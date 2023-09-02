-- Convert a .lua file to C array data, so that it can be #included within an
-- array initializer.
--
-- Lua seems to rely on a terminating zero byte when loading chunks in text
-- mode, even if it isn't included in the chunk size. This is likely a bug. We
-- work around it by appending a zero byte to the output.

local io = require 'io'
local os = require 'os'
local package = require 'package'
local string = require 'string'

local dirsep = package.config:sub(1, 1)

-- Same as assert(), but without adding error position information.
function check(...)
    local cond, msg = ...
    if cond then return ... end
    error(msg, 0)
end

-- Return the module name for the given path.
function module_name(path)
    return path:gsub(('^.*%s([^%s]*)%%.lua$'):format(dirsep, dirsep), '%1')
end

function main(input_path, output_path)
    -- Compile the input file.
    local name = '=<unknown>'
    if input_path ~= '-' then
        io.input(input_path)
        name = '@' .. module_name(input_path)
    end
    local chunk = check(load(function() return io.read(4096) end, name))
    local bin = string.dump(chunk)

    -- Output the compiled chunk as C array data.
    if output_path ~= '-' then io.output(output_path) end
    local offset = 0
    while true do
        if offset >= #bin then break end
        for i = 1, 16 do
            local v = bin:byte(offset + i)
            if v == nil then break end
            check(io.write(('0x%02x,'):format(v)))
        end
        check(io.write('\n'))
        offset = offset + 16
    end
    check(io.write('0x00,\n'))
    check(io.close())
end

local ok, err = pcall(main, ...)
if not ok then
    io.stderr:write(("ERROR: %s\n"):format(err))
    os.exit(1, true)
end
