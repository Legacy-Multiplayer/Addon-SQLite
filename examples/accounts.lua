-- Require this file once from your main script, before onMainScriptInit.
-- Requires Addon-SQLite, Addon-Player and Addon-Messaging.
-- Names identify demo accounts. Use authenticated account IDs in a real server.

local database
local ready = false
local players = {}
local startingMoney = 1000
local messageColor = 0xFFFFFFFF

local function isCurrent(playerId, player)
    return players[playerId] == player and doesPlayerExist(playerId)
end

local function reportFailure(operation, problem)
    print(string.format("[SQLite example] %s failed: %s", operation, tostring(problem)))
end

local function applyMoney(playerId, player, money, message)
    if not isCurrent(playerId, player) then
        return
    end

    player.money = money
    player.loaded = true
    setPlayerMoney(playerId, money)
    sendMessage(playerId, messageColor, string.format("%s Balance: $%d.", message, money))
end

local function savePlayer(playerId, player, money, message)
    player.busy = true

    local request, problem = executeDatabase(database, [[
        INSERT INTO example_accounts (name, money) VALUES (?, ?)
        ON CONFLICT(name) DO UPDATE SET money = excluded.money
    ]], {player.name, money}, function(success, result, errorMessage)
        if not success then
            reportFailure("save", errorMessage)
        end

        if not isCurrent(playerId, player) then
            return
        end

        player.busy = false

        if success then
            applyMoney(playerId, player, money, message)
        else
            sendMessage(playerId, messageColor, "Saving failed. Try again; no money was awarded.")
        end
    end)

    if not request then
        player.busy = false
        reportFailure("queue save", problem)

        if isCurrent(playerId, player) then
            sendMessage(playerId, messageColor, "Saving is unavailable. Try again shortly.")
        end
    end
end

local function loadPlayer(playerId, player)
    player.busy = true

    local request, problem = queryDatabase(database,
        "SELECT money FROM example_accounts WHERE name = ? LIMIT 1",
        {player.name}, function(success, rows, errorMessage)
            if not success then
                reportFailure("load", errorMessage)
            end

            if not isCurrent(playerId, player) then
                return
            end

            player.busy = false

            if not success then
                sendMessage(playerId, messageColor, "Loading failed. Use /load to retry.")
            elseif rows[1] then
                applyMoney(playerId, player, rows[1].money, "Account loaded.")
            else
                savePlayer(playerId, player, startingMoney, "Account created.")
            end
        end)

    if not request then
        player.busy = false
        reportFailure("queue load", problem)

        if isCurrent(playerId, player) then
            sendMessage(playerId, messageColor, "Loading is unavailable. Use /load to retry.")
        end
    end
end

local function availablePlayer(playerId, requireLoaded)
    local player = players[playerId]

    if not ready then
        sendMessage(playerId, messageColor, "The database is not ready yet.")
    elseif not player then
        sendMessage(playerId, messageColor, "Your account is unavailable. Reconnect to retry.")
    elseif player.busy then
        sendMessage(playerId, messageColor, "Your previous request is still being processed.")
    elseif requireLoaded and not player.loaded then
        sendMessage(playerId, messageColor, "Load your account with /load first.")
    else
        return player
    end
end

addEventHandler("onMainScriptInit", function()
    database = assert(openDatabase("data/example-accounts.sqlite"))

    assert(executeDatabase(database, [[
        CREATE TABLE IF NOT EXISTS example_accounts (
            name TEXT PRIMARY KEY,
            money INTEGER NOT NULL CHECK (money BETWEEN 0 AND 2000000000)
        )
    ]], {}, function(success, result, problem)
        if not success then
            reportFailure("create table", problem)
            return
        end

        ready = true

        -- Players may connect while the table is being created asynchronously.
        for playerId, player in pairs(players) do
            loadPlayer(playerId, player)
        end
    end))
end)

addEventHandler("onPlayerConnect", function(playerId)
    local player = {
        name = getPlayerName(playerId),
        money = 0,
        loaded = false,
        busy = false,
    }

    players[playerId] = player

    if ready then
        loadPlayer(playerId, player)
    end
end)

addEventHandler("onPlayerSpawn", function(playerId)
    local player = players[playerId]

    if player and player.loaded then
        setPlayerMoney(playerId, player.money)
    end
end)

addEventHandler("onPlayerDisconnect", function(playerId)
    local player = players[playerId]

    -- Do not overwrite an in-flight write with the older cached balance.
    -- Player getters may no longer be valid during disconnection.
    if player and player.loaded and not player.busy then
        savePlayer(playerId, player, player.money, "Account saved.")
    end

    players[playerId] = nil
end)

assert(registerCommand("balance", function(playerId)
    local player = availablePlayer(playerId, true)

    if player then
        sendMessage(playerId, messageColor, string.format("Balance: $%d. Try /earn, /save or /load.", player.money))
    end
end))

-- Test-only command: grant money only after SQLite confirms the write.
assert(registerCommand("earn", function(playerId)
    local player = availablePlayer(playerId, true)

    if player then
        savePlayer(playerId, player, math.min(player.money + 25, 2000000000), "Balance increased and saved.")
    end
end))

assert(registerCommand("save", function(playerId)
    local player = availablePlayer(playerId, true)

    if player then
        savePlayer(playerId, player, getPlayerMoney(playerId), "Account saved.")
    end
end))

assert(registerCommand("load", function(playerId)
    local player = availablePlayer(playerId, false)

    if player then
        loadPlayer(playerId, player)
    end
end))

addEventHandler("onMainScriptExit", function()
    ready = false
    players = {}
    database = nil
end)

-- Script exit cancels queued work. Save during play and wait for confirmation
-- before stopping/restarting; an exit handler cannot flush pending writes.
