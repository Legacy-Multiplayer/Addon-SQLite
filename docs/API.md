# SQLite functions

Load the native Addon-SQLite plugin in `setup.json`. These are global Lua functions; do not copy or require a `sqlite.lua` file.

## openDatabase(path)

Reserves a handle for a file under `resources`. Allowed extensions: `.sqlite`, `.sqlite3`, `.db`. Actual file opening happens on the worker during the first operation. A duplicate open path is rejected.

Return: `databaseId, ""` on success; `nil, errorMessage` on rejection.

```lua
local database, problem = openDatabase("data/accounts.sqlite")
assert(database, problem)
```

## executeDatabase(databaseId, sql, parameters?, callback?)

Queues one write/schema statement. Use `?` placeholders and a dense array of scalar parameters. Use `table.pack(...)` when a parameter is SQL NULL, including trailing NULL values. Passing `nil` for the parameter table means no parameters. To provide a callback without parameters, use `{}` or `nil` in that position. Supported values: boolean, integer, number, string, nil. Text preserves embedded NUL bytes and signed 64-bit integers remain integers.

Return: `requestId, ""` when queued; `false, errorMessage` for invalid/closing handles or full queues. Malformed argument types/shapes raise a Lua error. The ID confirms queue acceptance, not successful SQL execution.

Callback: `function(success, result, errorMessage)`; errorMessage is nil on success. Successful result: `{changes = count, lastInsertRowId = id}`. Without a callback, runtime failures are logged.

```lua
assert(executeDatabase(database, "INSERT INTO accounts(name, money, note) VALUES (?, ?, ?)",
    table.pack("Alex", 250, nil), function(success, result, problem)
        if not success then
            print(problem)
            return
        end
        print(result.changes)
    end))
```

## queryDatabase(databaseId, sql, parameters?, callback?)

Queues one read-only query. The same parameter/return/error rules apply as executeDatabase. Rows are an array of tables keyed by column name; use SQL `AS` aliases for duplicate names. SQL NULL appears as Lua nil (the key is absent). Empty query results return an empty table. BLOB values require `hex(column)` or a text conversion. Never iterate nullable result fields to infer the selected schema.

Return: queued request ID or `false, errorMessage`.

```lua
assert(queryDatabase(database, "SELECT name, money FROM accounts WHERE name = ? LIMIT 1", {"Alex"},
    function(success, rows, problem)
        if success and rows[1] then
            print(rows[1].name, rows[1].money)
        elseif not success then
            print(problem)
        end
    end))
```

## transactionDatabase(databaseId, statements, callback?)

Queues 1–128 writes atomically. Each statement is `{sql = "...", parameters = {...}}`; parameters may be omitted. A statement failure or failed commit rolls back the transaction. Queries and RETURNING are not supported here.

Return: request ID or `false, errorMessage`. Callback result on success is an array of per-statement `{changes, lastInsertRowId}` tables.

```lua
assert(transactionDatabase(database, {
    {sql = "UPDATE accounts SET money = money - ? WHERE name = ?", parameters = {50, "Alex"}},
    {sql = "UPDATE accounts SET money = money + ? WHERE name = ?", parameters = {50, "Sam"}},
}, function(success, results, problem)
    if not success then
        print(problem)
    end
end))
```

## closeDatabase(databaseId, callback?)

Rejects new operations immediately, then queues closing behind existing work. Its callback receives `success, {}, nil` on success. Files close on a worker.

Return: `true, ""` when queued; `false, errorMessage` on rejection.

```lua
assert(closeDatabase(database, function(success, result, problem)
    if not success then
        print(problem)
    end
end))
```

## Threading, limits and cleanup

Operations on one connection are sequential. Different connections may progress concurrently; callback order between them is unspecified. Lua callbacks are invoked only on the server thread. Individual script cleanup invalidates that script's retained callbacks; main-script exit resets this plugin's open handles and queues. Plugin unload cancels/joins its workers before the DLL is released.

The host's table/callback transport has bounded size and depth. SQLite limits responses to 1024 rows, 1024 total cells and 1 MiB aggregate data to stay within that budget. Use LIMIT/OFFSET or keyset pagination. Requests allow 256 KiB, SQL 64 KiB, 256 parameters and 128 transaction statements. There are at most 16 handles and 128 outstanding operations. Cancellation and the five-second SQLite deadline are cooperative; do not depend on a hard filesystem timeout.
