#include <stdbool.h>

#include "hardware/irq.h"
#include "hardware/structs/nvic.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#include "lua.h"
#include "lauxlib.h"
#include "mlua/event.h"
#include "mlua/util.h"

static bool is_user_irq(uint irq) {
    return FIRST_USER_IRQ <= irq && irq < FIRST_USER_IRQ + NUM_USER_IRQS;
}

static uint check_irq(lua_State* ls, int index) {
    lua_Integer irq = luaL_checkinteger(ls, index);
    luaL_argcheck(ls, 0 <= irq && irq < (lua_Integer)NUM_IRQS, index,
                  "unsupported IRQ number");
    return irq;
 }

static uint check_user_irq(lua_State* ls, int index) {
    lua_Integer irq = luaL_checkinteger(ls, index);
    luaL_argcheck(ls, is_user_irq(irq), index, "unsupported user IRQ number");
    return irq;
}

#if LIB_MLUA_MOD_MLUA_EVENT

typedef struct IRQState {
    MLuaEvent events[NUM_USER_IRQS];
    uint8_t pending;
} IRQState;

static_assert(NUM_USER_IRQS <= 8 * sizeof(uint8_t),
              "pending bitmask too small");

static IRQState uirq_state[NUM_CORES];

static inline MLuaEvent* user_irq_event(uint irq) {
    return &uirq_state[get_core_num()].events[irq - FIRST_USER_IRQ];
}

static void __time_critical_func(handle_user_irq)(void) {
    uint num = __get_current_exception() - VTABLE_FIRST_IRQ - FIRST_USER_IRQ;
    IRQState* state = &uirq_state[get_core_num()];
    uint32_t save = save_and_disable_interrupts();
    state->pending |= 1u << num;
    mlua_event_set_nolock(state->events[num]);
    restore_interrupts(save);
}

static int irq_thread_1(lua_State* ls, int status, lua_KContext ctx);
static int irq_thread_2(lua_State* ls, bool timeout);
static int irq_thread_done(lua_State* ls);

static int irq_thread(lua_State* ls) {
    // Set up a deferred to clean up on exit.
    lua_pushvalue(ls, lua_upvalueindex(1));
    lua_pushcclosure(ls, &irq_thread_done, 1);
    lua_toclose(ls, -1);

    return irq_thread_1(ls, LUA_OK, 0);
}

static int irq_thread_1(lua_State* ls, int status, lua_KContext ctx) {
    uint irq = lua_tointeger(ls, lua_upvalueindex(1));
    return mlua_event_wait(ls, *user_irq_event(irq), &irq_thread_2, 0);
}

static int irq_thread_2(lua_State* ls, bool timeout) {
    IRQState* state = &uirq_state[get_core_num()];
    uint8_t mask = 1u << (lua_tointeger(ls, lua_upvalueindex(1))
                          - FIRST_USER_IRQ);
    uint32_t save = save_and_disable_interrupts();
    uint8_t pending = state->pending;
    state->pending &= ~mask;
    restore_interrupts(save);
    if ((pending & mask) != 0) {  // Call the handler
        lua_pushvalue(ls, lua_upvalueindex(2));
        lua_pushvalue(ls, lua_upvalueindex(1));
        lua_callk(ls, 1, 1, 0, &irq_thread_1);
    }
    return -1;
}

static int irq_thread_done(lua_State* ls) {
    uint irq = lua_tointeger(ls, lua_upvalueindex(1));
    irq_remove_handler(irq, &handle_user_irq);
    lua_pushnil(ls);
    MLuaEvent* ev = user_irq_event(irq);
    lua_rawsetp(ls, LUA_REGISTRYINDEX, ev);
    mlua_event_unclaim(ls, ev);
    return 0;
}

static int set_handler(lua_State* ls, lua_Integer priority) {
    // Set the IRQ handler.
    uint irq = check_user_irq(ls, 1);
    MLuaEvent* ev = user_irq_event(irq);
    char const* err = mlua_event_claim(ev);
    if (err != NULL) return luaL_error(ls, "IRQ%d: %s", irq, err);
    mlua_event_set_irq_handler(irq, &handle_user_irq, priority);

    // Start the event handler thread.
    lua_pushthread(ls);
    luaL_getmetafield(ls, -1, "start");
    lua_remove(ls, -2);
    lua_pushvalue(ls, 1);  // num
    lua_pushvalue(ls, 2);  // handler
    lua_pushcclosure(ls, &irq_thread, 2);
    lua_call(ls, 1, 1);
    lua_pushvalue(ls, -1);
    lua_rawsetp(ls, LUA_REGISTRYINDEX, ev);
    return 1;
}

static int mod_set_handler(lua_State* ls) {
    return set_handler(ls, mlua_event_parse_irq_priority(ls, 3, -1));
}

static int mod_set_exclusive_handler(lua_State* ls) {
    return set_handler(ls, -1);
}

static int mod_add_shared_handler(lua_State* ls) {
    lua_Integer priority = luaL_checkinteger(ls, 3);
    luaL_argcheck(ls,
                  PICO_SHARED_IRQ_HANDLER_LOWEST_ORDER_PRIORITY <= priority &&
                  priority <= PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY,
                  3, "invalid priority");
    return set_handler(ls, priority);
}

static int mod_remove_handler(lua_State* ls) {
    lua_rawgetp(ls, LUA_REGISTRYINDEX, user_irq_event(check_user_irq(ls, 1)));
    luaL_getmetafield(ls, -1, "kill");
    lua_rotate(ls, -2, 1);
    lua_call(ls, 1, 0);
    return 0;
}

#endif  // LIB_MLUA_MOD_MLUA_EVENT

static int mod_clear(lua_State* ls) {
    uint irq = check_irq(ls, 1);
#if LIB_MLUA_MOD_MLUA_EVENT
    if (is_user_irq(irq)) {
        IRQState* state = &uirq_state[get_core_num()];
        uint num = irq - FIRST_USER_IRQ;
        uint32_t save = save_and_disable_interrupts();
        irq_clear(irq);
        state->pending &= ~(1u << num);
        mlua_event_clear(state->events[num]);
        restore_interrupts(save);
    } else {
#endif
        irq_clear(irq);
#if LIB_MLUA_MOD_MLUA_EVENT
    }
#endif
    return 0;
}

static int mod_is_pending(lua_State* ls) {
    uint irq = check_irq(ls, 1);
    bool pending = (nvic_hw->icpr & (1u << irq)) != 0;
#if LIB_MLUA_MOD_MLUA_EVENT
    if (!pending && is_user_irq(irq)) {
        IRQState* state = &uirq_state[get_core_num()];
        uint num = irq - FIRST_USER_IRQ;
        uint32_t save = save_and_disable_interrupts();
        if (state->pending & (1u << num)) pending = true;
        restore_interrupts(save);
    }
#endif
    lua_pushboolean(ls, pending);
    return 1;
}

static int mod_set_enabled(lua_State* ls) {
    uint irq = check_user_irq(ls, 1);
    bool enabled = mlua_to_cbool(ls, 2);
#if LIB_MLUA_MOD_MLUA_EVENT
    if (enabled && is_user_irq(irq)) {
        // Clear pending state before enabling. irq_set_enabled() does the same.
        mod_clear(ls);
    }
#endif
    irq_set_enabled(irq, enabled);
    return 0;
}

MLUA_FUNC_0_2(mod_, irq_, set_priority, check_irq, luaL_checkinteger)
MLUA_FUNC_1_1(mod_, irq_, get_priority, lua_pushinteger, check_irq)
MLUA_FUNC_1_1(mod_, irq_, is_enabled, lua_pushboolean, check_irq)
MLUA_FUNC_0_2(mod_, irq_, set_mask_enabled, luaL_checkinteger, mlua_to_cbool)
MLUA_FUNC_1_1(mod_, irq_, has_shared_handler, lua_pushboolean, check_irq)
MLUA_FUNC_0_1(mod_, irq_, set_pending, check_irq)
MLUA_FUNC_0_1(mod_,, user_irq_claim, check_user_irq)
MLUA_FUNC_1_1(mod_,, user_irq_claim_unused, lua_pushinteger, mlua_to_cbool)
MLUA_FUNC_0_1(mod_,, user_irq_unclaim, check_user_irq)

static MLuaSym const module_syms[] = {
    MLUA_SYM_V(TIMER_IRQ_0, integer, TIMER_IRQ_0),
    MLUA_SYM_V(TIMER_IRQ_1, integer, TIMER_IRQ_1),
    MLUA_SYM_V(TIMER_IRQ_2, integer, TIMER_IRQ_2),
    MLUA_SYM_V(TIMER_IRQ_3, integer, TIMER_IRQ_3),
    MLUA_SYM_V(PWM_IRQ_WRAP, integer, PWM_IRQ_WRAP),
    MLUA_SYM_V(USBCTRL_IRQ, integer, USBCTRL_IRQ),
    MLUA_SYM_V(XIP_IRQ, integer, XIP_IRQ),
    MLUA_SYM_V(PIO0_IRQ_0, integer, PIO0_IRQ_0),
    MLUA_SYM_V(PIO0_IRQ_1, integer, PIO0_IRQ_1),
    MLUA_SYM_V(PIO1_IRQ_0, integer, PIO1_IRQ_0),
    MLUA_SYM_V(PIO1_IRQ_1, integer, PIO1_IRQ_1),
    MLUA_SYM_V(DMA_IRQ_0, integer, DMA_IRQ_0),
    MLUA_SYM_V(DMA_IRQ_1, integer, DMA_IRQ_1),
    MLUA_SYM_V(IO_IRQ_BANK0, integer, IO_IRQ_BANK0),
    MLUA_SYM_V(IO_IRQ_QSPI, integer, IO_IRQ_QSPI),
    MLUA_SYM_V(SIO_IRQ_PROC0, integer, SIO_IRQ_PROC0),
    MLUA_SYM_V(SIO_IRQ_PROC1, integer, SIO_IRQ_PROC1),
    MLUA_SYM_V(CLOCKS_IRQ, integer, CLOCKS_IRQ),
    MLUA_SYM_V(SPI0_IRQ, integer, SPI0_IRQ),
    MLUA_SYM_V(SPI1_IRQ, integer, SPI1_IRQ),
    MLUA_SYM_V(UART0_IRQ, integer, UART0_IRQ),
    MLUA_SYM_V(UART1_IRQ, integer, UART1_IRQ),
    MLUA_SYM_V(ADC_IRQ_FIFO, integer, ADC_IRQ_FIFO),
    MLUA_SYM_V(I2C0_IRQ, integer, I2C0_IRQ),
    MLUA_SYM_V(I2C1_IRQ, integer, I2C1_IRQ),
    MLUA_SYM_V(RTC_IRQ, integer, RTC_IRQ),
    MLUA_SYM_V(FIRST_USER_IRQ, integer, FIRST_USER_IRQ),
    MLUA_SYM_V(NUM_USER_IRQS, integer, NUM_USER_IRQS),
    MLUA_SYM_V(DEFAULT_IRQ_PRIORITY, integer, PICO_DEFAULT_IRQ_PRIORITY),
    MLUA_SYM_V(LOWEST_IRQ_PRIORITY, integer, PICO_LOWEST_IRQ_PRIORITY),
    MLUA_SYM_V(HIGHEST_IRQ_PRIORITY, integer, PICO_HIGHEST_IRQ_PRIORITY),
    MLUA_SYM_V(SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY, integer, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY),
    MLUA_SYM_V(SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY, integer, PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY),
    MLUA_SYM_V(SHARED_IRQ_HANDLER_LOWEST_ORDER_PRIORITY, integer, PICO_SHARED_IRQ_HANDLER_LOWEST_ORDER_PRIORITY),

    MLUA_SYM_F(set_priority, mod_),
    MLUA_SYM_F(get_priority, mod_),
    MLUA_SYM_F(set_enabled, mod_),
    MLUA_SYM_F(is_enabled, mod_),
    MLUA_SYM_F(set_mask_enabled, mod_),
#if LIB_MLUA_MOD_MLUA_EVENT
    MLUA_SYM_F(set_handler, mod_),
    MLUA_SYM_F(set_exclusive_handler, mod_),
    // irq_get_exclusive_handler: not useful in Lua
    MLUA_SYM_F(add_shared_handler, mod_),
    MLUA_SYM_F(remove_handler, mod_),
#endif
    MLUA_SYM_F(has_shared_handler, mod_),
    // irq_get_vtable_handler: not useful in Lua
    MLUA_SYM_F(clear, mod_),
    MLUA_SYM_F(set_pending, mod_),
    MLUA_SYM_F(is_pending, mod_),
    MLUA_SYM_F(user_irq_claim, mod_),
    MLUA_SYM_F(user_irq_claim_unused, mod_),
    MLUA_SYM_F(user_irq_unclaim, mod_),
};

#if LIB_MLUA_MOD_MLUA_EVENT

static __attribute__((constructor)) void init(void) {
    for (uint core = 0; core < NUM_CORES; ++core) {
        IRQState* state = &uirq_state[get_core_num()];
        for (uint i = 0; i < NUM_USER_IRQS; ++i) {
            state->events[i] = MLUA_EVENT_UNSET;
        }
    }
}

#endif  // LIB_MLUA_MOD_MLUA_EVENT

int luaopen_hardware_irq(lua_State* ls) {
    mlua_event_require(ls);

    mlua_new_module(ls, 0, module_syms);
    return 1;
}
