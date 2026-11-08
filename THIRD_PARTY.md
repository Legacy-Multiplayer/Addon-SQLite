# Dependency notices

## Shared Addon-SDK

`SDK` is a pinned Git submodule of https://github.com/Legacy-Multiplayer/Addon-SDK. Its own license is retained inside the submodule; the root MIT license covers this plugin's code.

## SQLite 3.54.1

`vendor/sqlite/sqlite3.c` and `sqlite3.h` come from the official amalgamation archive:

https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip

Archive SHA3-256:

```text
92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
```

The archive digest was verified before extraction. SQLite is public domain; its source files retain the upstream notice. Upstream license: https://www.sqlite.org/copyright.html. Compile-time authorization follows https://www.sqlite.org/c3ref/set_authorizer.html; the plugin uses it to block operations that bypass its path/transaction policy.

## Local code

The database engine, native plugin and examples are project code under the root MIT license. No game assets or account data are bundled.
