# Working player save/load example

`accounts.lua` is a runnable persistence demo. Its functions are called by connection/spawn/disconnection events and four commands; there are no unused save/load helpers.

## Set it up

1. Use a minimal test server with **Addon-SQLite**, **Addon-Player** and **Addon-Messaging** enabled in `setup.json`.
2. Copy `accounts.lua` into your script's module directory, for example `main/accounts.lua`.
3. Load it once from `main/main.lua`, before `onMainScriptInit` fires:

   ```lua
   require("accounts")
   ```

4. Keep your normal player spawn setup. This example handles persistence, not spawning or authentication. Rename its commands if they conflict with your gamemode.

The example creates `resources/data/example-accounts.sqlite` automatically. The file belongs to the server; players do not download it.

## Try it

| Action | Expected result |
| --- | --- |
| Join with a new player name | Create a record and receive $1,000 after the write succeeds. |
| `/balance` | Display the loaded balance. |
| `/earn` | Add $25, save it, then apply the confirmed balance to the player's wallet. |
| `/save` | Save the current game wallet returned by `getPlayerMoney`. |
| `/load` | Read the stored balance and apply it to the game wallet. |
| Disconnect and reconnect with the same name | Load the previously saved balance. |
| Respawn | Reapply the loaded balance without creating a new account. |

For a quick test: join, wait for **Account created**, run `/earn`, wait for the save confirmation, then reconnect. Your balance should be **$1,025**. Stop and restart the server after a confirmed save, then reconnect to check persistence across restarts.

## What the example handles

- Waits for schema creation before loading players, including connections received while initialization is still running.
- Creates missing records and loads existing records using bound SQL parameters.
- Allows one outstanding account request per player. Money from `/earn` is awarded only after a successful database write.
- Checks the original player-state table before applying asynchronous results. A disconnected player's callback cannot update a newcomer who receives the same numeric player ID.
- Saves the confirmed cached balance on disconnect, unless a newer write is already in progress. It avoids player getters during disconnect and avoids overwriting that newer write with an old value.
- Reports queue rejection and SQL errors. A load failure does not silently create a replacement account.

## Adapting it to a real server

This is a **name-keyed demonstration, not a login system**. In production, load after authentication and use a stable authenticated account ID as the key. Remove the free-money `/earn` command.

The demo owns its cached balance. If other scripts change the game wallet directly, call `/save` while connected or integrate those changes into the persistence flow; the disconnect handler cannot safely discover arbitrary external changes after the player has left.

Save important changes during play. Main-script exit cancels queued operations, so do not queue your only save in an exit handler. Wait for completion before an intentional shutdown/restart. The plugin cleans up its handles on script exit; this example does not close the shared connection while players are still using it.
