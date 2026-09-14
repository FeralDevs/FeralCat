-- MP3 Player: portable app logic. SD access and decoding stay in native audio.
-- Commands are asynchronous. Local queue position never follows stale status.
local screen = "main"
local snap = nil
local generation = nil
local lastFinished = 0
local requestedPlayback = nil
local queue = {}
local order = {}
local cursor = 0
local queueName = "All tracks"
local shuffle = false
local repeatMode = 0 -- 0=off, 1=all, 2=one
local armed = false
local pending = nil
local building = nil
local scanPending = false
local playlist = 0
local pageOffset = 0
local page = nil
local needPage = false
local notice = ""
local elapsed = 0
local wantVolume = nil
local wantSeek = nil
local closed = false
local dirty = true
local equalizerPage = 0
local optionParent = "sound"
local wantEqualizer = nil
local names = {idle="Ready", scanning="Scanning", loading="Loading track",
    playing="Playing", paused="Paused", stopped="Stopped", ended="Track ended", error="Error"}
local equalizers = {"Neutral", "Voice", "Warm", "Small speaker"}
local repeats = {"Off", "All", "One"}
local playerView = {title="", subtitle="", status="", position=0, duration=0, playing=false, volume=35}
local cachedRawTitle, cachedTitle = nil, ""

-- Keep UTF-8 code points intact, replace invalid bytes/control characters.
-- Native text is already bounded, and this also bounds every UI string.
local function cut(value, limit)
    local s = type(value) == "string" and value or ""
    local out, used, i = {}, 0, 1
    while i <= #s and used < limit do
        local b = string.byte(s, i)
        local n = 1
        local valid = true
        if b >= 194 and b <= 223 then n = 2
        elseif b >= 224 and b <= 239 then n = 3
        elseif b >= 240 and b <= 244 then n = 4
        elseif b >= 128 then valid = false end
        if i + n - 1 > #s then valid = false end
        if valid and n > 1 then
            for j = 1, n - 1 do
                local c = string.byte(s, i + j)
                if c < 128 or c > 191 then valid = false end
            end
            local c = string.byte(s, i + 1)
            if (b == 224 and c < 160) or (b == 237 and c >= 160)
                or (b == 240 and c < 144) or (b == 244 and c >= 144) then valid = false end
        end
        if not valid then
            out[#out + 1] = "?"; used = used + 1; i = i + 1
        elseif b < 32 or b == 127 or (b == 194 and string.byte(s, i + 1) < 160) then
            out[#out + 1] = " "; used = used + 1; i = i + n
        elseif used + n > limit then break
        else
            out[#out + 1] = string.sub(s, i, i + n - 1)
            used = used + n; i = i + n
        end
    end
    return table.concat(out)
end

local function clock(seconds)
    seconds = math.max(0, math.floor(seconds or 0))
    local rest = seconds % 60
    return tostring(math.floor(seconds / 60)) .. ":" .. (rest < 10 and "0" or "") .. tostring(rest)
end

local function item(id, label) return { id = id, label = cut(label, 63) } end
local function current() return queue[order[cursor]] end
local function repeatName() return repeats[repeatMode + 1] end

-- Reuse menu tables. Status polling must not rebuild unchanged controls.
local menus = {
    library={item("all", "All tracks"), item("lists", "Playlists"), item("library_options", "More options"), item("back", "Back")},
    library_options={item("scan", "Rescan music"), item("options", "Playback options"), item("close", "Exit player"), item("back", "Back")},
    sound={item("volume", "Volume"), item("equalizer", "Equalizer"), item("options", "Playback options"), item("back", "Back")},
    volume={item("quieter", "Quieter -5"), item("louder", "Louder +5"), item("back", "Back")},
    options={item("shuffle", "Shuffle: Off"), item("repeat", "Repeat: Off"), item("seek", "Seek / Stop"), item("back", "Back")},
    seek={item("seek_back", "Back 10 s"), item("seek_forward", "Forward 10 s"), item("stop", "Stop playback"), item("back", "Back")},
    confirm_close={item("cancel_close", "Keep listening"), item("confirm_close", "Exit player")},
    equalizer={item("eq0", "Neutral"), item("eq1", "Voice"), item("eq_next", "More presets"), item("back", "Back")},
    waiting={item("back", "Back")}
}

local function command(action, value)
    if not meow.audio.command(action, value) then
        dirty = notice ~= "Busy. Please try again."
        notice = "Busy. Please try again."
        return false
    end
    dirty = true
    notice = ""
    return true
end

local function makeOrder(selected)
    dirty = true
    order = {}
    for i = 1, #queue do order[i] = i end
    cursor = selected or 1
    if shuffle and #order > 1 then
        order[1], order[cursor] = order[cursor], order[1]
        for i = #order, 3, -1 do
            local j = math.random(2, i)
            order[i], order[j] = order[j], order[i]
        end
        cursor = 1
    end
end

local function playAt(position)
    if not order[position] then return false end
    local id = queue[order[position]]
    if not command("play", id) then return false end
    cursor = position
    armed = true
    wantSeek = nil
    -- This app exclusively submits valid Play commands to its FIFO service.
    -- Count accepted requests so A,A and A,B,A cannot acknowledge an older A.
    requestedPlayback = ((requestedPlayback or 0) + 1) % 4294967296
    pending = { id = id, playback = requestedPlayback }
    return true
end

local function advance(direction, natural)
    if #order == 0 or building or scanPending then return end
    if natural and repeatMode == 2 then playAt(cursor); return end
    local position = cursor + direction
    if position > #order then
        if natural and repeatMode == 0 then armed = false; return end
        position = 1
    elseif position < 1 then position = #order end
    playAt(position)
end

local function rescan()
    if command("scan") then
        queue, order, cursor = {}, {}, 0
        armed, pending, building = false, nil, nil
        scanPending = true
        page, needPage = nil, false
        notice = "Scanning the SD card ..."
    end
end

local function beginQueue(list, selected, name)
    dirty = true
    building = { playlist = list, selected = selected, name = name, ids = {},
                 offset = 0, generation = generation }
    screen = "main"
    notice = "Loading the queue ..."
end

-- At most eight IDs per tick; a 512-track queue never blocks one callback.
local function buildStep()
    local job = building
    if not job then return end
    if job.generation ~= generation then building = nil; return end
    local result = meow.audio.tracks(job.playlist, job.offset, 8)
    if not result then return end
    dirty = true
    local total = math.min(result.total, 512)
    if total < 1 then
        building = nil; notice = "No playable tracks."; return
    end
    for i = 1, #result.items do
        if #job.ids < total then job.ids[#job.ids + 1] = result.items[i].id end
    end
    dirty = true
    job.offset = job.offset + #result.items
    if #result.items == 0 and #job.ids < total then
        building = nil; notice = "List changed. Please rescan."; return
    end
    if #job.ids >= total then
        queue = job.ids
        queueName = cut(job.name, 63)
        makeOrder(math.min(job.selected, #queue))
        building = nil
        playAt(cursor)
    end
end

local function browse(kind, list, offset)
    dirty = true
    screen, playlist, pageOffset = kind, list or 0, offset or 0
    page, needPage = nil, true
end

local function pageStep()
    if not needPage or scanPending then return end
    if screen == "tracks" then page = meow.audio.tracks(playlist, pageOffset, 2)
    elseif screen == "playlists" then page = meow.audio.playlists(pageOffset, 2)
    else needPage = false; return end
    if page then needPage = false; dirty = true end
end

local function back()
    dirty = true
    if screen == "main" then notice = "Hold B to exit."
    elseif screen == "tracks" and playlist > 0 then browse("playlists")
    elseif screen == "tracks" or screen == "playlists" then screen = "library"
    elseif screen == "library_options" or screen == "confirm_close" then screen = "library"
    elseif screen == "volume" or screen == "equalizer" then screen = "sound"
    elseif screen == "options" then screen = optionParent
    elseif screen == "seek" then screen = "options"
    else screen = "main" end
end

local function render()
    if not dirty then return end
    dirty = false
    local s = snap or {state="idle", position=0, duration=0, volume=35, tracks=0, playlists=0}
    local actions = menus[screen] or menus.waiting
    local title, body = "MP3 Player", ""
    if screen == "main" then
        local rawTitle = s.title and s.title ~= "" and s.title or "Choose music"
        if rawTitle ~= cachedRawTitle then cachedRawTitle = rawTitle; cachedTitle = cut(rawTitle, 95) end
        playerView.title = cachedTitle
        playerView.subtitle = tostring(cursor) .. "/" .. tostring(#queue) .. "  " .. queueName
        if s.state == "error" and not pending then playerView.status = cut(s.error, 95)
        elseif building then playerView.status = "Loading queue: " .. tostring(#building.ids) .. " tracks"
        elseif notice ~= "" then playerView.status = cut(notice, 95)
        else playerView.status = pending and "Loading track" or (names[s.state] or "Ready") end
        playerView.position, playerView.duration = s.position, s.duration
        playerView.playing = s.state == "playing" and not pending
        playerView.volume = wantVolume or s.volume
        meow.ui.player(playerView)
        return
    elseif screen == "library" then
        title = "Library"
        body = tostring(s.tracks) .. " tracks, " .. tostring(s.playlists) .. " playlists"
        if scanPending then body = "Scanning music ..." end
    elseif screen == "library_options" then
        title = "More options"
        body = notice ~= "" and notice or "Rescan music or exit the player."
    elseif screen == "sound" then
        title = "Sound"
        body = "Volume " .. tostring(wantVolume or s.volume) .. "%  |  " .. equalizers[(wantEqualizer or s.equalizer or 0) + 1]
    elseif screen == "volume" then
        title = "Volume"
        body = "Volume: " .. tostring(wantVolume or s.volume) .. "%"
        if notice ~= "" then body = body .. "  " .. notice end
    elseif screen == "options" then
        title = "Playback options"
        body = cut(queueName, 63)
        actions[1].label = "Shuffle: " .. (shuffle and "On" or "Off")
        actions[2].label = "Repeat: " .. repeatName()
    elseif screen == "seek" then
        title = "Seek / Stop"
        body = notice ~= "" and notice or (clock(s.position) .. " / " .. clock(s.duration))
    elseif screen == "equalizer" then
        title = "Equalizer"
        body = "Current: " .. equalizers[(wantEqualizer or s.equalizer or 0) + 1]
        if notice ~= "" then body = body .. "  " .. notice end
        for i = 1, 2 do
            local preset = equalizerPage + i - 1
            actions[i].id, actions[i].label = "eq" .. tostring(preset), equalizers[preset + 1]
        end
        actions[3].label = equalizerPage == 0 and "More presets" or "First page"
    elseif screen == "confirm_close" then
        title = "Exit player?"
        body = "Playback will stop."
    else
        title = screen == "playlists" and "Playlists" or "Choose a track"
        body = page and (tostring(pageOffset + 1) .. "-" .. tostring(pageOffset + #page.items)
               .. " of " .. tostring(page.total)) or "Loading the list ..."
        if page and page.total == 0 then body = "No entries. Copy MP3/M3U files to the SD card." end
        if page then
            actions = {}
            for i = 1, #page.items do
                local row = page.items[i]
                local label = tostring(pageOffset + i) .. ". " .. cut(row.title, 49)
                if screen == "playlists" then label = label .. " (" .. tostring(row.count) .. ")" end
                actions[#actions + 1] = item("pick" .. tostring(i), label)
            end
            if pageOffset + #page.items < page.total then actions[#actions + 1] = item("page_next", "Next page") end
            if pageOffset > 0 and pageOffset + #page.items >= page.total then actions[#actions + 1] = item("page_first", "First page") end
            actions[#actions + 1] = item("back", "Back")
        end
    end
    meow.ui.menu(title, cut(body, 127), actions, screen ~= "tracks" and screen ~= "playlists")
end

local function poll()
    local s = meow.audio.status()
    if not s then return end
    if not snap or s.state ~= snap.state or s.generation ~= snap.generation or s.error ~= snap.error
        or s.tracks ~= snap.tracks or s.playlists ~= snap.playlists
        or ((screen == "main" or screen == "seek") and (s.position ~= snap.position or s.duration ~= snap.duration))
        or (screen == "main" and (s.title ~= snap.title or s.track ~= snap.track))
        or ((screen == "main" or screen == "sound" or screen == "volume") and s.volume ~= snap.volume)
        or ((screen == "sound" or screen == "equalizer") and s.equalizer ~= snap.equalizer) then dirty = true end
    local changed = generation ~= nil and generation ~= s.generation
    generation = s.generation
    if requestedPlayback == nil or changed then requestedPlayback = s.playback end
    if changed then
        queue, order, cursor = {}, {}, 0
        armed, pending, building, scanPending = false, nil, nil, false
        page = nil
        needPage = screen == "tracks" or screen == "playlists"
        -- Playlist IDs belong to one generation; return to library on rescan.
        if playlist > 0 and screen == "tracks" then screen = "library"; needPage = false end
        notice = "Scan complete: " .. tostring(s.tracks) .. " tracks."
    end
    if wantVolume == s.volume then wantVolume = nil end
    if wantEqualizer == s.equalizer then wantEqualizer = nil end
    if wantSeek and math.abs(s.position - wantSeek) <= 1 then wantSeek = nil end
    local newFinish = s.finished ~= lastFinished
    lastFinished = s.finished
    snap = s
    if s.state == "scanning" then scanPending = true end
    local acknowledged = pending and s.track == pending.id and s.playback == pending.playback
    if acknowledged then
        if s.state == "playing" or s.state == "paused" then
            dirty = true
            pending = nil
            newFinish = false -- This is the first acknowledgement of our Play.
        elseif s.state == "ended" then
            dirty = true
            pending = nil -- Very short track can finish between status polls.
        end
    end
    -- Errors from an older request cannot cancel a queued retry.
    local failure = s.state == "error" and (not pending or acknowledged)
    if failure then
        if armed or pending or notice ~= "Press Play or Next to retry." then dirty = true end
        armed, pending = false, nil
        scanPending = false
        notice = "Press Play or Next to retry."
    elseif newFinish and armed and not pending and not building and not scanPending
        and s.state == "ended" and s.track == current() then
        advance(1, true)
    end
    buildStep()
    pageStep()
end

function on_start()
    assert(meow.system.has("audio"), "Audio service unavailable. Update the firmware.")
    assert(meow.system.has("player_ui"), "Player interface unavailable. Update the firmware.")
    meow.app.capture_back(true)
    math.randomseed(meow.system.uptime_ms())
    poll()
    if snap and snap.tracks == 0 and snap.state ~= "scanning" then rescan() end
    render()
end

function on_tick(dt)
    elapsed = elapsed + dt
    if elapsed < 200 then
        if building then buildStep(); render() end
        return
    end
    elapsed = 0
    poll()
    render()
end

function on_event(kind, value)
    dirty = true
    if kind == "key" then
        if value == "back" then back()
        elseif screen == "main" and value == "left" then advance(-1, false)
        elseif screen == "main" and value == "right" then advance(1, false)
        elseif (screen == "tracks" or screen == "playlists") and page then
            if value == "left" and pageOffset > 0 then browse(screen, playlist, pageOffset - 2)
            elseif value == "right" and pageOffset + #page.items < page.total then browse(screen, playlist, pageOffset + 2) end
            pageStep()
        end
        render(); return
    end
    if kind ~= "action" then return end
    if value == "confirm_close" then meow.app.exit(); return
    elseif value == "close" then screen = "confirm_close"
    elseif value == "cancel_close" then screen = "library_options"
    elseif value == "back" then back()
    elseif value == "library" then screen = "library"
    elseif value == "library_options" then screen = "library_options"
    elseif value == "sound" then screen = "sound"
    elseif value == "volume" then screen = "volume"
    elseif value == "equalizer" then screen = "equalizer"; equalizerPage = 0
    elseif value == "eq_next" then equalizerPage = equalizerPage == 0 and 2 or 0
    elseif string.sub(value, 1, 2) == "eq" then
        local preset = tonumber(string.sub(value, 3))
        if preset and preset >= 0 and preset <= 3 and command("eq", preset) then wantEqualizer = preset; screen = "sound" end
    elseif value == "options" then optionParent = screen; screen = "options"
    elseif value == "seek" then screen = "seek"
    elseif value == "all" then browse("tracks", 0)
    elseif value == "lists" then browse("playlists")
    elseif value == "scan" then rescan()
    elseif value == "page_first" then browse(screen, playlist, 0)
    elseif value == "page_next" and page then browse(screen, playlist, pageOffset + 2)
    elseif string.sub(value, 1, 4) == "pick" and page then
        local index = tonumber(string.sub(value, 5))
        local row = index and page.items[index]
        if row then
            if screen == "playlists" then browse("tracks", row.id)
            elseif screen == "tracks" then beginQueue(playlist, pageOffset + index,
                playlist == 0 and "All tracks" or "M3U playlist") end
        end
    elseif value == "play" then
        if building or scanPending then notice = "Please wait for the list to load."
        elseif pending then notice = "Loading track."
        elseif armed and snap and (snap.state == "playing" or snap.state == "paused") then command("pause")
        elseif #queue > 0 then playAt(cursor)
        else beginQueue(0, 1, "All tracks") end
    elseif value == "next" then advance(1, false)
    elseif value == "previous" then advance(-1, false)
    elseif value == "louder" or value == "quieter" then
        local previousVolume = wantVolume or (snap and snap.volume) or 35
        local volume = math.max(0, math.min(100, previousVolume
            + (value == "louder" and 5 or -5)))
        if volume == previousVolume then dirty = false
        elseif command("volume", volume) then wantVolume = volume end
    elseif value == "seek_forward" or value == "seek_back" then
        if snap and (snap.state == "playing" or snap.state == "paused")
            and not pending and not scanPending and not building then
            if snap.duration <= 0 then
                notice = "Duration not known yet. Please wait."
            else
                local seconds = math.max(0, math.min(65535, (wantSeek or snap.position)
                    + (value == "seek_forward" and 10 or -10)))
                seconds = math.min(seconds, snap.duration)
                if command("seek", seconds) then wantSeek = seconds end
            end
        else notice = "Seeking is available while playing or paused." end
    elseif value == "stop" then
        if command("stop") then
            armed, pending, building = false, nil, nil
            wantSeek = nil
            notice = "Stopping playback."
        end
    elseif value == "shuffle" then
        local selected = order[cursor] or 1
        shuffle = not shuffle
        if #queue > 0 then makeOrder(selected) end
    elseif value == "repeat" then repeatMode = (repeatMode + 1) % 3 end
    pageStep()
    render()
end

function on_stop()
    if not closed then meow.audio.command("stop"); closed = true end
end
