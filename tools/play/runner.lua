-- The emulator's side of tools/playtest.py: PCSX-Redux runs this file with
-- no window. It reads a run's plan (plan.txt, written by the tool: what to
-- watch, the pad's presses, the checkpoints), follows it one vertical blank
-- at a time, and writes what happened to run.log, a line a thing. The tool
-- reads that; nothing is decided here but when a checkpoint is reached and
-- whether the game has crashed.
--
-- The plan is data read while this runs, never Lua the tool writes: a
-- chunk of LuaJIT's holds 65,536 constants at most, and a script past that
-- fails as it loads.
--
-- The plan's lines (words apart by spaces; addresses and numbers decimal):
--   limit N            the run ends at vertical blank N at the latest
--   stall N            a crash: the game's frame count still for N blanks
--   alive ADDR         where the game's frame count is (4 bytes)
--   movie ADDR VALUE   but not while the two bytes at ADDR hold VALUE: the
--                      opening movie's player counts no frames
--   text LO HI         the program's code, for the check of the pc
--   var NAME ADDR SIZE SIGNED        a value to write down at checkpoints
--   in CP OFFSET PAD BUTTON HOLD     a press, OFFSET blanks after checkpoint
--                                    number CP was reached (0: after power on)
--   cp NAME SHOT WITHIN at N         a checkpoint at vertical blank N
--   cp NAME SHOT WITHIN after N      ... N blanks after the one before
--   cp NAME SHOT WITHIN when V OP X [V OP X ...]
--                                    ... when variable number V compares so
--                                    with X, every one of them; given up
--                                    WITHIN blanks after the one before
-- The log's lines:
--   start / set ...                  the run began; a setting of the emulator
--   hb FRAME PC ALIVE                every 300 blanks
--   cp NAME FRAME VALUE...           a checkpoint, with every var's value
--   shot NAME WIDTH HEIGHT BPP       a picture written to NAME.raw
--   miss NAME FRAME                  a checkpoint not reached in time
--   crash WHY FRAME ...              pc / exception / stall
--   limit FRAME                      the run's last blank came
--   done FRAME                       the last checkpoint was reached
--   error TEXT                       this script failed
local DIR = PLAYTEST_DIR
local log = assert(io.open(DIR .. 'run.log', 'w'))
local function out(s)
    log:write(s, '\n')
    log:flush()
end

local finished = false
local function finish(code)
    if finished then return end
    finished = true
    log:close()
    PCSX.quit(code)
end

-- Anything that goes wrong in this file is a line of the log and the end
-- of the run, never an error the emulator has to report.
local function guarded(f)
    return function(...)
        if finished then return true end
        local ok, err = xpcall(f, debug.traceback, ...)
        if not ok then
            out('error ' .. tostring(err):gsub('%s+', ' '))
            finish(3)
            return true
        end
        return err
    end
end

local frame = 0
local limit, stall, alive_at, text_lo, text_hi = 0, 1800, nil, 0, 0
local movie_at, movie_value
local vars, checkpoints, presses = {}, {}, {}
local due = {} -- [frame] = {{pad, button, down}, ...}
local next_cp, last_cp_frame = 1, 0
local alive_value, alive_since = -1, 0
local mem, scratch, registers, pads, BUTTON

local function read(addr, size, signed)
    local base, at = mem, bit.band(addr, 0x1FFFFF)
    if bit.rshift(addr, 12) == 0x1F800 then base, at = scratch, bit.band(addr, 0x3FF) end
    local v = 0
    for i = size - 1, 0, -1 do v = v * 256 + base[at + i] end
    if signed and v >= 2 ^ (size * 8 - 1) then v = v - 2 ^ (size * 8) end
    return v
end

local function load_plan()
    for line in io.lines(DIR .. 'plan.txt') do
        local w = {}
        for word in line:gmatch('%S+') do w[#w + 1] = word end
        local kind = w[1]
        if kind == 'limit' then
            limit = tonumber(w[2])
        elseif kind == 'stall' then
            stall = tonumber(w[2])
        elseif kind == 'alive' then
            alive_at = tonumber(w[2])
        elseif kind == 'movie' then
            movie_at, movie_value = tonumber(w[2]), tonumber(w[3])
        elseif kind == 'text' then
            text_lo, text_hi = tonumber(w[2]), tonumber(w[3])
        elseif kind == 'var' then
            vars[#vars + 1] = {addr = tonumber(w[3]), size = tonumber(w[4]), signed = w[5] == '1'}
        elseif kind == 'in' then
            presses[#presses + 1] = {cp = tonumber(w[2]), offset = tonumber(w[3]), pad = tonumber(w[4]),
                                     button = w[5], hold = tonumber(w[6])}
        elseif kind == 'cp' then
            local cp = {name = w[2], shot = w[3] == '1', within = tonumber(w[4]), how = w[5], tests = {}}
            if cp.how == 'when' then
                for i = 6, #w, 3 do
                    cp.tests[#cp.tests + 1] = {var = tonumber(w[i]), op = w[i + 1], value = tonumber(w[i + 2])}
                end
            else
                cp.frames = tonumber(w[6])
            end
            checkpoints[#checkpoints + 1] = cp
        elseif kind ~= nil then
            error('plan.txt: a line of a kind not known: ' .. line)
        end
    end
    if limit <= 0 then error('plan.txt gives no limit') end
end

local function value_of(index)
    local v = vars[index]
    return read(v.addr, v.size, v.signed)
end

local COMPARE = {
    ['=='] = function(a, b) return a == b end,
    ['!='] = function(a, b) return a ~= b end,
    ['<'] = function(a, b) return a < b end,
    ['<='] = function(a, b) return a <= b end,
    ['>'] = function(a, b) return a > b end,
    ['>='] = function(a, b) return a >= b end,
}

local function reached(cp)
    if cp.how == 'at' then return frame >= cp.frames end
    if cp.how == 'after' then return frame >= last_cp_frame + cp.frames end
    for _, t in ipairs(cp.tests) do
        if not COMPARE[t.op](value_of(t.var), t.value) then return false end
    end
    return true
end

local function shot(name)
    local s = PCSX.GPU.takeScreenShot()
    local f = Support.File.open(DIR .. name .. '.raw', 'TRUNCATE')
    f:writeMoveSlice(s.data)
    f:close()
    out(string.format('shot %s %d %d %d', name, s.width, s.height, (tonumber(s.bpp) == 0) and 16 or 24))
end

local function values()
    local t = {}
    for i = 1, #vars do t[i] = string.format('%d', value_of(i)) end
    return table.concat(t, ' ')
end

local function schedule(cp_index, from)
    for _, p in ipairs(presses) do
        if p.cp == cp_index then
            local down, up = from + p.offset, from + p.offset + p.hold
            due[down] = due[down] or {}
            due[up] = due[up] or {}
            table.insert(due[down], {p.pad, p.button, true})
            table.insert(due[up], {p.pad, p.button, false})
        end
    end
end

-- The end of a run that went wrong: what the game's state and picture were.
local function fail(line)
    out(line)
    out('cp (failure) ' .. frame .. ' ' .. values())
    pcall(shot, 'failure')
    finish(0)
end

-- Where the processor may be: the kernel's memory, the program's code, the
-- movie overlay's place, the BIOS.
local function pc_is_sane(pc)
    local at = bit.band(pc, 0x1FFFFFFF)
    if at < 0x10000 then return true end
    if at >= text_lo and at < text_hi then return true end
    if at >= 0x1D8800 and at < 0x200000 then return true end
    return at >= 0x1FC00000 and at < 0x1FC80000
end

local on_blank = guarded(function()
    frame = frame + 1
    local pc = registers.pc
    if not pc_is_sane(pc) then
        return fail(string.format('crash pc %d 0x%08X', frame, pc))
    end
    if alive_at then
        local now = read(alive_at, 4, false)
        if now ~= alive_value or (movie_at and read(movie_at, 2, false) == movie_value) then
            alive_value, alive_since = now, frame
        end
        if frame - alive_since >= stall then
            return fail(string.format('crash stall %d the frame count has been %d since %d', frame, now, alive_since))
        end
    end
    if frame % 300 == 0 then out(string.format('hb %d 0x%08X %d', frame, pc, alive_value)) end

    local list = due[frame]
    if list then
        for _, p in ipairs(list) do
            if p[3] then pads[p[1]].setOverride(BUTTON[p[2]]) else pads[p[1]].clearOverride(BUTTON[p[2]]) end
        end
        due[frame] = nil
    end

    while next_cp <= #checkpoints do
        local cp = checkpoints[next_cp]
        if reached(cp) then
            out('cp ' .. cp.name .. ' ' .. frame .. ' ' .. values())
            if cp.shot then shot(cp.name) end
            schedule(next_cp, frame)
            last_cp_frame = frame
            next_cp = next_cp + 1
        elseif cp.within > 0 and frame - last_cp_frame > cp.within then
            return fail('miss ' .. cp.name .. ' ' .. frame)
        else
            break
        end
    end
    if next_cp > #checkpoints then
        out('done ' .. frame)
        return finish(0)
    end
    if frame >= limit then
        if next_cp <= #checkpoints then return fail('miss ' .. checkpoints[next_cp].name .. ' ' .. frame) end
        out('limit ' .. frame)
        return finish(0)
    end
end)

-- Every exception goes through this address. An interrupt (0) and a system
-- call (8) are the game's everyday; anything else is a crash.
local on_exception = guarded(function()
    local cause = registers.CP0.r[13]
    local code = bit.band(bit.rshift(cause, 2), 31)
    if code ~= 0 and code ~= 8 then
        fail(string.format('crash exception %d code %d at 0x%08X', frame, code, registers.CP0.r[14]))
    end
    return true
end)

guarded(function()
    load_plan()
    mem, scratch, registers = PCSX.getMemPtr(), PCSX.getScratchPtr(), PCSX.getRegisters()
    pads = {PCSX.SIO0.slots[1].pads[1], PCSX.SIO0.slots[2].pads[1]}
    BUTTON = PCSX.CONSTS.PAD.BUTTON
    for _, p in ipairs(presses) do
        if BUTTON[p.button] == nil then error('plan.txt: no button ' .. p.button) end
    end
    -- No sound, as fast as the host allows (the sound's clock is what
    -- holds the emulator to the console's speed), both pads plugged in.
    -- A setting this build of the emulator does not have is written down
    -- and the run goes on.
    local function set(what, f)
        local ok, err = pcall(f)
        out('set ' .. what .. ' ' .. (ok and 'ok' or ('failed: ' .. tostring(err):gsub('%s+', ' '))))
    end
    set('mute', function() PCSX.settings.spu.Mute = true end)
    set('speed', function() PCSX.settings.spu.Speed = 0 end)
    set('pad2', function() PCSX.settings.pads[2].Connected = true end)
    schedule(0, 0)
    -- The listeners are kept in globals so that they are not collected.
    PLAYTEST_BLANK = PCSX.Events.createEventListener('GPU::Vsync', on_blank)
    PLAYTEST_EXCEPTION = PCSX.addBreakpoint(0x80000080, 'Exec', 4, 'exception', on_exception)
    out('start')
end)()
