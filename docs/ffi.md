# Calling C from Emojicode, and what FFI cannot do

Emojicode calls C through the `c` package (`📦 c 🌊`). This page lists the limits of the interface. The tests that pin
them are `tests/compilation/ffi*.emojic`, `tests/host/ffiHostLib.*`, `tests/importing/ffiPackage*` and
`tests/reject/cExport*.emojic`.

## Declaring C functions

`🎍🌊 🐇☣️❗️ name params ➡️ result 📻 🔤symbol🔤` binds a C function with a fixed prototype. Every call uses the
C calling convention for exactly the parameters you declare, and must happen inside `☣️`. The types are those of the
`c` package (`🔶🌊🔢` `int`, `🔶🌊🐘` `long`, `🔶🌊🦕🔸🔼` `unsigned long long`, `🔶🌊🎈` `float`, `💯` `double`,
`👌` `_Bool`, `🔶🌊📏` `size_t`, …; `💯`, `👌`, `🔢` and `💧` come from `s` without the `🌊` prefix). Use the C type that the prototype has, not a wider one: a narrow type such as
`short` or `unsigned char` is passed and returned with C's width and wraps as C does.

`🔗 🔤file.c🔤 🔗` compiles a C source next to the Emojicode source and links it in. The path is relative to the
Emojicode file. A package built with `-p` carries its native objects and the trampolines for its `🎍🌊`
declarations in its archive, so a program importing the package does not import `c` and does not compile the C
source again.

## Structs

* A `🎍🌊` value type is a C struct. It can be **passed to and returned from** C functions by value, which
  the compiler implements with a trampoline.
* C functions **written in Emojicode** (with `📻`, called by C) cannot take or return structs by value, which is
  rejected at compile time. Pass a `📍` to the struct instead.
* Structs can be passed by pointer both ways.

## Callables

A `🍇🎍🌊 … 🍉` callable type is a C function pointer. Only a `🍇🎍🌊 … 🍉` literal is a C callable: ordinary
Emojicode closures cannot be converted to one, and the literal cannot capture variables. A C callable can only be
called inside `☣️`.

## Strings and pointers

* `🔡🕊🔶🌊🧶` copies a NUL-terminated C string into a `🔡` and stops at the first 0 byte. It expects UTF-8 and does not
  validate it (see #212). `🔡🕊🔶🌊🧶 string count` copies exactly *count* bytes, including 0 bytes.
* `🧶🕊🔶🌊🧶` passes a NUL-terminated copy of a `🔡` that is only valid while its block runs. A `🔡` with an embedded
  0 byte is seen by C as ending there.
* A `📍` is not managed. Storing a managed object through one does not follow the ownership rules described in
  `c/c.🍇` (see #211); do not rely on it.

## Limits

* **Variadic functions** (`printf`, `open`, `ioctl`, …) can be declared, but are called with the non-variadic
  calling convention, which is wrong on some targets (see #181). Write a C wrapper with a fixed prototype.
* **`errno`** is not visible. Write a C function that returns it, or the result and error code through a `📍`.
* **Unsigned 64-bit values** have no literals (see #143, #180). Convert with `🆕🔶🌊🦕🔸🔼▶️🔢`, which wraps as C does.
* Struct layout and calling conventions are those of the target; test on each target you support.
