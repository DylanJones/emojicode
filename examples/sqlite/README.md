# SQLite from Emojicode

This example wraps the SQLite C library in Emojicode, using only Emojicode and the C interface, with no C or C++
code of its own:

- `sqlite.🍇` is the `sqlite` package. The value type `🗄` declares the SQLite functions it uses with `🎍🌊`,
  exactly as `sqlite3.h` declares them. The exported types `🗃` (a database), `🗒` (a prepared statement), `💎` (a
  value) and `💥` (an error) wrap them safely.
- `main.🍇` creates a table in an in-memory database and runs a few queries: inserts through a prepared statement,
  a query with a bound parameter, aggregates, an update, a query through `sqlite3_exec` with a callback written in
  Emojicode, and a query that fails.

It shows most of the C interface: C types from the `c` package (`🔶🌊🔢`, `🔶🌊📏`, …), opaque pointers (`🕳`),
out-parameters through `📍🐚🍬🕳🍆`, nullable pointers, converting strings with `🧶`, a C callback (`🍇🎍🌊 … 🍉`) with
an object passed as user data (`🕳 ▶️📤`, `👀`, `📥`), and linking a system library with `🔗 🔤sqlite3🔤 🔗`.

## Building

SQLite must be installed: macOS includes it, and on Debian or Ubuntu install `libsqlite3-dev`. Then run

```sh
./build.sh
```

To use a compiler that is not installed, pass its path and its packages:

```sh
EMOJICODEC=../../build/Compiler/emojicodec ./build.sh -S ../../build
```

The script builds the package into `packages/sqlite`, builds and runs `main`, and compares its output with
`expected.txt`.
