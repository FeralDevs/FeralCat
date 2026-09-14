-- A separately installed app: no firmware rebuild when this file changes.
local count = 0

local function render()
    meow.ui.show("Hello Meow", "This app was loaded from the SD card.\n\nCount: " .. tostring(count), {
        { id = "increment", label = "+1" },
        { id = "reset", label = "Reset" },
        { id = "close", label = "Close app" }
    })
end

function on_start()
    assert(meow.api_version == 1)
    assert(meow.system.has("ui") and meow.system.has("system"))
    render()
end

function on_event(kind, value)
    if kind == "action" then
        if value == "increment" then count = count + 1
        elseif value == "reset" then count = 0
        elseif value == "close" then meow.app.exit(); return
        end
        render()
    elseif kind == "key" then
        if value == "right" then count = count + 1
        elseif value == "left" then count = count - 1
        end
        render()
    end
end
