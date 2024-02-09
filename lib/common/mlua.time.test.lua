-- Copyright 2024 Remy Blank <remy@c-space.org>
-- SPDX-License-Identifier: MIT

_ENV = module(...)

local math = require 'math'
local int64 = require 'mlua.int64'
local time = require 'mlua.time'
local string = require 'string'
local table = require 'table'

function test_ticks(t)
    t:expect(t:expr(time).ticks_per_second):eq(1e6)
    t:expect(t:expr(time).ticks64())
        :gte(time.min_ticks):lte(time.max_ticks)
    t:expect(t:expr(time).ticks()):apply(math.type):op('type is'):eq('integer')

    for _, fn in ipairs{'ticks', 'ticks64'} do
        local ticks = time[fn]
        for i = 1, 10 do
            local t1 = ticks()
            local t2 = ticks()
            while t2 == t1 do t2 = ticks() end
            t:expect(t2 - t1):label("t2 - t1 (%s)", fn):gt(0):lt(100)
        end
    end
end

local integer_bits = string.packsize('j') * 8

function test_to_ticks64(t)
    for _, test in ipairs{
        {0, int64('0'), int64('0')},
        {0x12345678, int64('0x1a2b3c4d11111111'), int64('0x1a2b3c4d12345678')},
        {0x7fffffff, int64('0x1a2b3c4d00000000'), int64('0x1a2b3c4d7fffffff')},
        {0x80000000, int64('0x1a2b3c4d00000000'), int64('0x1a2b3c4c80000000')},
        {0x7ffffffe, int64('0x1a2b3c4dffffffff'), int64('0x1a2b3c4e7ffffffe')},
        {0x7fffffff, int64('0x1a2b3c4dffffffff'), int64('0x1a2b3c4d7fffffff')},
    } do
        local ti, now, want = table.unpack(test)
        if integer_bits < 64 then
            t:expect(t.expr(time).to_ticks64(ti, now)):eq(want)
            t:expect(time.to_ticks64(ti, now) - now)
                :label("tick64(%s, now) - now", ti)
                :gte(math.mininteger):lte(math.maxinteger)
        else
            t:expect(t.expr(time).to_ticks64(ti, now)):eq(ti)
        end
    end
end

function test_sleep_BNB(t)
    local delay = 2000
    for _, fn in ipairs{'ticks', 'ticks64'} do
        local ticks = time[fn]
        local t1 = ticks()
        time.sleep_until(t1 + delay)
        local t2 = ticks()
        t:expect(t2 - t1):label("sleep_until(%s) duration (%s)", t1 + delay, fn)
            :gte(delay):lt(delay + 250)

        local t1 = ticks()
        time.sleep_for(0 * t1 + delay)
        local t2 = ticks()
        t:expect(t2 - t1):label("sleep_for(%s) duration (%s)", delay, fn)
            :gte(delay):lt(delay + 250)
    end
end
