SQLite persistence for **Legacy Multiplayer servers**, with native Lua functions, background SQL execution and no separate database service.

## Features

- Five global Lua functions: `openDatabase`, `executeDatabase`, `queryDatabase`, `transactionDatabase` and `closeDatabase`.
- Ordinary Lua parameter tables, returned row tables and callbacks. No `sqlite.lua`, `require("sqlite")` or JSON adapter.
- Ordered operations per database, with SQL on background workers and callbacks on the server thread.
- Bound parameters, atomic transactions, signed 64-bit integers and strings containing embedded NUL bytes.
- Database files under the server's `resources` directory. SQLite is linked into the plugin; no additional SQLite DLL is required.

## Downloads and installation

| Package | Platform | Library |
| --- | --- | --- |
| `Addon-SQLite-windows-x86.zip` | Windows, 32-bit | `Addon-SQLite.dll` |
| `Addon-SQLite-linux-x86.tar.gz` | Linux, 32-bit | `Addon-SQLite.so` |

Stop the server, extract the matching library into its `addons` directory, enable it in `setup.json`, then restart. Each package includes documentation, Lua examples and a manifest recording its source and SDK commits.

Windows requires the matching **x86 MSVC runtime**. Linux builds use an **Ubuntu 22.04 x86** baseline and require compatible 32-bit C/C++ runtimes.

## Saving and loading

See the [player persistence example](https://github.com/Legacy-Multiplayer/Addon-SQLite/tree/main/examples) for setup, commands and a reconnect test. Use `?` placeholders with parameter tables; use `table.pack(...)` for SQL NULL parameters. Returned SQL NULL fields are Lua `nil`.

Opening reserves a handle; the first queued operation opens the file. A returned request ID confirms **queue acceptance**, not successful SQL execution. Check the callback before applying a result to gameplay.

## Compatibility and limits

- Native addon API version remains **1**, but the exact SDK fingerprint must match the server. Rebuild against the matching [Addon-SDK](https://github.com/Legacy-Multiplayer/Addon-SDK) revision when required.
- Up to 16 handles, 128 outstanding operations, 256 parameters and 128 statements per transaction.
- Requests: 256 KiB; SQL: 64 KiB. Query results: 1,024 rows, 1,024 total cells and 1 MiB aggregate data. Paginate larger reads.
- Operations use a cooperative five-second progress deadline and 250 ms busy timeout; these are not hard filesystem I/O deadlines.
- Save during play and wait for confirmation before shutdown. Script exit cancels queued work. Revalidate player/account identity before applying asynchronous results.

Both Windows and Linux packages are built by GitHub Actions. Build success does not establish compatibility with every server/runtime configuration.

**Documentation:** [Lua API](https://github.com/Legacy-Multiplayer/Addon-SQLite/blob/main/docs/API.md) · [Building](https://github.com/Legacy-Multiplayer/Addon-SQLite/blob/main/docs/BUILDING.md) · [Examples](https://github.com/Legacy-Multiplayer/Addon-SQLite/tree/main/examples)

Building from source? Clone with `--recurse-submodules`; GitHub's automatic source archives do not include the SDK submodule contents.
