-- Copyright 2023 Remy Blank <remy@c-space.org>
-- SPDX-License-Identifier: MIT

_ENV = mlua.module(...)

local io = require 'mlua.io'
local list = require 'mlua.list'
local stdio = require 'mlua.stdio'
local testing_stdio = require 'mlua.testing.stdio'
local util = require 'mlua.util'

function test_streams_Y(t)
    for _, test in ipairs{
        {false, stdio.stdout, {'a', '', 'bc\n', 'def'}, 'abc\ndef'},
        {true, stdio.stdout, {'a', '', 'bc\n', 'def'}, 'abc\r\ndef'},
        {false, stdio.stderr, {'uvw\n', 'xyz'}, 'uvw\nxyz'},
        {true, stdio.stderr, {'uvw\n', 'xyz'}, 'uvw\r\nxyz'},
    } do
        local crlf, stream, writes, want = list.unpack(test)
        local wr, got = list(), ''
        t:expect(pcall(function()  -- No output in this block
            local done<close> = testing_stdio.enable_loopback(t, crlf)
            for _, w in ipairs(writes) do wr:append(stream:write(w)) end
            while #got < #want do
                got = got .. stdio.stdin:read(#want - #got)
            end
        end))
        for i, w in ipairs(writes) do
            t:expect(wr[i]):label("write(%s)", util.repr(w)):eq(#w)
        end
        t:expect(got):label("crlf: %s, got", crlf):eq(want)
    end
end

function test_print(t)
    local b = io.Buffer()
    t:patch(_G, 'stdout', b)

    local v = setmetatable({}, {__tostring = function() return '(v)' end})
    print(1, 2.3, "4-5", v)
    print(6, 7, 8)
    t:expect(tostring(b)):label('output')
        :eq("1\t" .. tostring(2.3) .."\t4-5\t(v)\n6\t7\t8\n")
end
