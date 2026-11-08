local database

addEventHandler("onMainScriptInit", function()
    database = assert(openDatabase("data/accounts.sqlite"))

    assert(executeDatabase(database, [[
        CREATE TABLE IF NOT EXISTS accounts (
            name TEXT PRIMARY KEY,
            money INTEGER NOT NULL DEFAULT 0,
            note TEXT
        )
    ]], {}, function(success, result, problem)
        if not success then
            print(problem)
        end
    end))
end)

-- Call after authentication. Never store plaintext passwords.
function loadAccount(name, callback)
    return queryDatabase(database, "SELECT name, money FROM accounts WHERE name = ? LIMIT 1", {name},
        function(success, rows, problem)
            callback(success, success and rows[1] or nil, problem)
        end)
end

function saveAccount(name, money)
    return executeDatabase(database, [[
        INSERT INTO accounts(name, money) VALUES (?, ?)
        ON CONFLICT(name) DO UPDATE SET money = excluded.money
    ]], {name, money}, function(success, result, problem)
        if not success then
            print(problem)
        end
    end)
end

-- The plugin cancels owned work on script exit. Queue important saves before
-- shutting down, and wait for their completion before requesting a restart.
