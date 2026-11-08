# Addon-SQLite

SQLite persistence for Legacy Server, using the shared [Addon-SDK](https://github.com/Legacy-Multiplayer/Addon-SDK). Five native Lua functions accept tables and callbacks directly. No Lua adapter, JSON codec, database service or separate SQLite DLL is needed.

SQL runs on background workers. Operations on each handle execute in FIFO order; callbacks return on the server thread. `openDatabase` validates the resource-relative path and reserves a handle; the first queued operation opens the file, so check its callback for opening failures.

```lua
addEventHandler("onMainScriptInit", function()
    local database = assert(openDatabase("data/accounts.sqlite"))

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
```

Paths are relative to the server's `resources` folder. Never accept SQL text directly from players; bind their values through parameters.

| Function | Purpose |
| --- | --- |
| `openDatabase(path)` | Reserve a database handle. |
| `closeDatabase(databaseId, callback?)` | Close after accepted operations finish. |
| `executeDatabase(databaseId, sql, parameters?, callback?)` | Execute one statement without returned rows. |
| `queryDatabase(databaseId, sql, parameters?, callback?)` | Execute one read-only query. |
| `transactionDatabase(databaseId, statements, callback?)` | Commit writes together, or roll them back. |

See [API](docs/API.md), [building](docs/BUILDING.md) and [account persistence](examples/accounts.lua).

## Dependencies and lifecycle

Clone with `git clone --recurse-submodules <repository-url>` or run `git submodule update --init --recursive` in an existing checkout. `SDK` is pinned to a server-compatible commit. API version remains **1**, but the fingerprint must match: rebuild all plugins and the server together when updating it.

SQLite is vendored from its official amalgamation; provenance is in `THIRD_PARTY.md`. Normal open, SQL and explicit close operations run on workers; unload waits for owned work. Finalization of idle or cancelled connections may run during main-thread cleanup. Script exit cancels queued operations, invalidates callbacks and retires connections when in-flight work releases them. Handles are not reused during one plugin lifetime. An async result may outlive a player connection: revalidate that the result still belongs to the intended player before applying it.

Limits: 16 handles, 128 queued/running operations, 256 KiB per request, 64 KiB SQL, 256 parameters and 128 statements per transaction. Queries allow 1024 rows, 1024 total cells and 1 MiB aggregate data. Paginate larger reads. Each running operation has a cooperative five-second progress deadline and a 250 ms busy timeout. Filesystem I/O itself cannot be forcibly timed out by SQLite's progress handler.

Foreign keys are enabled; WAL and FULL synchronous durability are requested. Keep database, `-wal` and `-shm` files together while in use. PRAGMA, ATTACH/DETACH, manual transactions, extensions and virtual tables are blocked. Path checks reject traversal and observed symbolic links; trusted server code and filesystem permissions remain necessary.

## Layout

```text
SDK/          Pinned shared SDK submodule
src/          Native plugin and SQLite operations
vendor/sqlite/ Official SQLite amalgamation
examples/     Lua usage without an adapter
.github/      Windows/Linux x86 builds and draft release packaging
```

GitHub Actions builds both 32-bit platforms. A manual run with a new version tag creates a **draft** release after both builds pass. It never publishes a release automatically. Linux builds target Ubuntu 22.04's runtime baseline; Windows builds require the matching x86 MSVC runtime.
