-- Copyright 2023 Remy Blank <remy@c-space.org>
-- SPDX-License-Identifier: MIT

_ENV = mlua.Module(...)

local adc = require 'hardware.adc'
local time = require 'pico.time'

local function to_celsius(value)
    return 27.0 - (value * (3.3 / (1 << 12)) - 0.706) / 0.001721
end

local input = 4
local minT, maxT = 10.0, 40.0

function test_polling(t)
    adc.init()
    adc.set_temp_sensor_enabled(true)
    adc.select_input(input)
    local got = adc.get_selected_input()
    t:expect(got == input,
             "Unexpected selected input: got %s, want %s", got, input)
    for i = 1, 10 do
        local temp = to_celsius(adc.read())
        t:expect(minT <= temp and temp <= maxT,
                 "Temperature outside of [%.1f, %.1f]: %.1f", minT, maxT, temp)
        time.sleep_ms(1)
    end
end

function test_fifo_Y(t)
    adc.init()
    adc.set_temp_sensor_enabled(true)
    adc.select_input(input)
    adc.fifo_setup(true, false, 1, false, false)
    adc.set_clkdiv(48000 - 1)
    adc.fifo_drain()
    t:expect(adc.fifo_is_empty(), "FIFO isn't empty after draining")
    local got = adc.fifo_get_level()
    t:expect(got == 0, "FIFO has %s values, want 0")

    adc.fifo_enable_irq()
    adc.run(true)
    t:cleanup(function()
        adc.run(false)
        adc.fifo_enable_irq(false)
    end)
    time.sleep_ms(5)
    t:expect(not adc.fifo_is_empty(), "FIFO remains empty")
    t:expect(adc.fifo_get_level() ~= 0, "FIFO level remains 0")

    for i = 1, 10 do
        local temp = to_celsius(adc.fifo_get_blocking())
        t:expect(minT <= temp and temp <= maxT,
                 "Temperature outside of [%.1f, %.1f]: %.1f", minT, maxT, temp)
    end
end
