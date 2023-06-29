#include "hardware/timer.h"
#include "pico/platform.h"

#include "lua.h"
#include "lauxlib.h"
#include "mlua/event.h"
#include "mlua/int64.h"
#include "mlua/util.h"

static uint check_alarm(lua_State* ls, int index) {
    lua_Integer alarm = luaL_checkinteger(ls, index);
    luaL_argcheck(ls, 0 <= alarm && alarm < (lua_Integer)NUM_TIMERS, index,
                  "invalid alarm number");
    return alarm;
}

static absolute_time_t check_absolute_time(lua_State* ls, int arg) {
    return from_us_since_boot(mlua_check_int64(ls, arg));
}

#if LIB_MLUA_MOD_MLUA_EVENT

typedef struct AlarmState {
    MLuaEvent events[NUM_TIMERS];
    uint8_t pending;
} AlarmState;

static_assert(NUM_TIMERS <= 8 * sizeof(uint8_t), "pending bitmask too small");

static AlarmState alarm_state;

static void __time_critical_func(handle_alarm)(uint alarm) {
    uint32_t save = mlua_event_lock();
    alarm_state.pending |= 1u << alarm;
    mlua_event_unlock(save);
    mlua_event_set(alarm_state.events[alarm]);
}

static int mod_enable_alarm(lua_State* ls) {
    uint alarm = check_alarm(ls, 1);
    MLuaEvent* ev = &alarm_state.events[alarm];
    if (lua_type(ls, 2) == LUA_TBOOLEAN && !lua_toboolean(ls, 2)) {
        hardware_alarm_set_callback(alarm, NULL);
        mlua_event_unclaim(ls, ev);
        return 0;
    }
    char const* err = mlua_event_claim(ev);
    if (err != NULL) return luaL_error(ls, "TIMER%d: %s", alarm, err);
    hardware_alarm_set_callback(alarm, &handle_alarm);
    return 0;
}

static int try_pending(lua_State* ls) {
    uint8_t mask = 1u << lua_tointeger(ls, 1);
    uint32_t save = mlua_event_lock();
    uint8_t pending = alarm_state.pending;
    alarm_state.pending &= ~mask;
    mlua_event_unlock(save);
    return (pending & mask) != 0 ? 0 : -1;
}

static int mod_wait_alarm(lua_State* ls) {
    uint alarm = check_alarm(ls, 1);
    return mlua_event_wait(ls, alarm_state.events[alarm], &try_pending, 0);
}

#endif  // LIB_MLUA_MOD_MLUA_EVENT

static void cancel_alarm(uint alarm) {
    hardware_alarm_cancel(alarm);
#if LIB_MLUA_MOD_MLUA_EVENT
    uint32_t save = mlua_event_lock();
    alarm_state.pending &= ~(1u << alarm);
    mlua_event_unlock(save);
#endif
}

static int mod_set_target(lua_State* ls) {
    uint alarm = check_alarm(ls, 1);
    absolute_time_t t = check_absolute_time(ls, 2);
#if LIB_MLUA_MOD_MLUA_EVENT
    // Cancel first to avoid that the pending flag is set between resetting it
    // and setting the new target.
    cancel_alarm(alarm);
#endif
    lua_pushboolean(ls, hardware_alarm_set_target(alarm, t));
    return 1;
}

static int mod_cancel(lua_State* ls) {
    cancel_alarm(check_alarm(ls, 1));
    return 0;
}

MLUA_FUNC_1_0(mod_,, time_us_32, lua_pushinteger)
MLUA_FUNC_1_0(mod_,, time_us_64, mlua_push_int64)
MLUA_FUNC_0_1(mod_,, busy_wait_us_32, luaL_checkinteger)
MLUA_FUNC_0_1(mod_,, busy_wait_us, mlua_check_int64)
MLUA_FUNC_0_1(mod_,, busy_wait_ms, luaL_checkinteger)
MLUA_FUNC_0_1(mod_,, busy_wait_until, check_absolute_time)
MLUA_FUNC_1_1(mod_,, time_reached, lua_pushboolean, check_absolute_time)
MLUA_FUNC_0_1(mod_, hardware_alarm_, claim, luaL_checkinteger)
MLUA_FUNC_1_1(mod_, hardware_alarm_, claim_unused, lua_pushinteger,
              lua_toboolean)
MLUA_FUNC_0_1(mod_, hardware_alarm_, unclaim, luaL_checkinteger)
MLUA_FUNC_1_1(mod_, hardware_alarm_, is_claimed, lua_pushboolean,
              luaL_checkinteger)
MLUA_FUNC_0_1(mod_, hardware_alarm_, force_irq, check_alarm)

static MLuaSym const module_syms[] = {
    MLUA_SYM_F(time_us_32, mod_),
    MLUA_SYM_F(time_us_64, mod_),
    MLUA_SYM_F(busy_wait_us_32, mod_),
    MLUA_SYM_F(busy_wait_us, mod_),
    MLUA_SYM_F(busy_wait_ms, mod_),
    MLUA_SYM_F(busy_wait_until, mod_),
    MLUA_SYM_F(time_reached, mod_),
    MLUA_SYM_F(claim, mod_),
    MLUA_SYM_F(claim_unused, mod_),
    MLUA_SYM_F(unclaim, mod_),
    MLUA_SYM_F(is_claimed, mod_),
    // hardware_alarm_set_callback: See enable_alarm, wait_alarm
    MLUA_SYM_F(set_target, mod_),
    MLUA_SYM_F(cancel, mod_),
    MLUA_SYM_F(force_irq, mod_),
#if LIB_MLUA_MOD_MLUA_EVENT
    MLUA_SYM_F(enable_alarm, mod_),
    MLUA_SYM_F(wait_alarm, mod_),
#endif
};

#if LIB_MLUA_MOD_MLUA_EVENT

static __attribute__((constructor)) void init(void) {
    for (uint i = 0; i < NUM_TIMERS; ++i) {
        alarm_state.events[i] = MLUA_EVENT_UNSET;
    }
}

#endif  // LIB_MLUA_MOD_MLUA_EVENT

int luaopen_hardware_timer(lua_State* ls) {
    mlua_event_require(ls);
    mlua_require(ls, "mlua.int64", false);

    // Create the module.
    mlua_new_module(ls, 0, module_syms);
    return 1;
}
