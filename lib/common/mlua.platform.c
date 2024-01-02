// Copyright 2024 Remy Blank <remy@c-space.org>
// SPDX-License-Identifier: MIT

#include "mlua/platform.h"

#include "mlua/int64.h"
#include "mlua/module.h"
#include "mlua/util.h"

static void mod_min_time(lua_State* ls, MLuaSymVal const* value) {
    uint64_t min, max;
    mlua_platform_time_range(&min, &max);
    mlua_push_int64(ls, min);
}

static void mod_max_time(lua_State* ls, MLuaSymVal const* value) {
    uint64_t min, max;
    mlua_platform_time_range(&min, &max);
    mlua_push_int64(ls, max);
}

static int mod_time_us(lua_State* ls) {
    mlua_push_int64(ls, mlua_platform_time_us());
    return 1;
}

static void mod_flash(lua_State* ls, MLuaSymVal const* value) {
    MLuaFlash const* flash = mlua_platform_flash();
    if (flash == NULL)  {
        lua_pushboolean(ls, false);
        return;
    }
    lua_createtable(ls, 0, 4);
    mlua_push_intptr(ls, flash->address);
    lua_setfield(ls, -2, "address");
    mlua_push_intptr(ls, flash->size);
    lua_setfield(ls, -2, "size");
    mlua_push_intptr(ls, flash->write_size);
    lua_setfield(ls, -2, "write_size");
    mlua_push_intptr(ls, flash->erase_size);
    lua_setfield(ls, -2, "erase_size");
}

static void mod_binary_size(lua_State* ls, MLuaSymVal const* value) {
    mlua_push_intptr(ls, mlua_platform_binary_size());
}

MLUA_SYMBOLS(module_syms) = {
    MLUA_SYM_V(name, string, MLUA_ESTR(MLUA_PLATFORM)),
    MLUA_SYM_P(min_time, mod_),
    MLUA_SYM_P(max_time, mod_),
    MLUA_SYM_P(flash, mod_),
    MLUA_SYM_P(binary_size, mod_),

    MLUA_SYM_F(time_us, mod_),
};

MLUA_OPEN_MODULE(mlua.platform) {
    mlua_require(ls, "mlua.int64", false);
    mlua_new_module(ls, 0, module_syms);
    return 1;
}
