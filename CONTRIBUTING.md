# Contributing

You want to improve Emojicode? That's awesome! Before you start, we would like to tell you a few things.

We hope that these guidelines make Emojicode's development as fun as possible for everyone. By following these, you ensure that your and our time is not wasted and we can incorporate your contribution quickly and smoothly.

## Helpful Contributions

Since Emojicode is a rather complex programming language, we need to coordinate and discuss changes to language or any of its default packages. If you want to make a **change to the language or the s package**, we would like to ask you to **open an issue first**, so we can make sure your plans fit Emojicode on the long-term.

## Your First Contribution

Contributing is as easy as

1. Create your own fork of Emojicode.
2. Make changes and test your changes.
3. Send a pull request.

## Some Rules about New Code

- All code should be platform independent and in conformance with ISO C++14.
- Before submitting any pull request, make sure all tests pass. If you add a feature, add tests too.
- `docs/grammar.ebnf` describes exactly the syntax that the compiler accepts. If you change the lexer or the parser,
  update the grammar and run `ninja grammar`, which checks the grammar against the compiler.
- If you change what compiled packages and their importers must agree on, such as the layout of boxes, box infos,
  protocol conformances, type descriptions, value witnesses or class infos, a calling convention, or the format of
  interfaces (🏛), increase `kABIVersion` in `Compiler/Package/Package.hpp` and the version in the first line of each
  `tests/packages/*/interface.emojii`, except `abiVersionMismatch`, which must keep an older version, and
  `abiVersionMissing`, which must have none. Keep the CRLF line endings of `abiVersionCRLF`. The compiler then refuses
  to import packages compiled by an earlier version and asks for them to be recompiled.
- Try to follow the coding style established in the file you're editing.

## Adding a Test

`tests.py` finds compilation, library, host and importing tests on disk, so adding one of these is only a matter of
adding files, without editing `tests.py` itself:

- A **compilation test** is `tests/compilation/NAME.emojic` plus the output it must print, `NAME.txt`. A file that
  is only meant to be `📜`-included by another test, not compiled on its own, has no `NAME.txt`; list it under
  `formatted_includes` in `tests.py` instead, next to the test(s) that include it.
- A **library test** is `tests/s/NAME.emojic`; its exit code must be 0.
- A **host test** is `tests/host/NAME.emojic` plus the C program that calls its `🎍🌊` functions, `NAME.c`, and the
  output the linked program must print, `NAME.txt`.
- An **importing test** is `tests/importing/NAME.emojic` plus the package it imports, `NAMEPackage.🍇`, and the
  output the program must print, `NAME.txt`.
- A **reject test** (`tests/reject/*.emojic`, must fail to compile) or **parse test** (`tests/parse/*.emojic`, must
  parse) is found the same way as before.

A compilation test whose specializations or LLVM IR must be checked also gets a `NAME.specializations` or `NAME.ir`
file (see the comments above `specialization_tests`/`ir_tests` in `tests.py` for their format); this is picked up
automatically too.

A compilation or library test can start with a directive comment, on its own line among its leading `💭` comments:

    💭 test: unoptimized panic

A compilation test's tokens: `unoptimized` also compiles and runs it without optimizations; `panic` means its
program panics (aborts with SIGABRT) after printing what `NAME.txt` says; `stress` excludes it from quick and
valgrind runs because it takes seconds to run; `leak_check` also runs it, at both optimized and unoptimized settings,
with `EMOJICODE_CHECK_DESCRIPTION_LEAKS` set, so that an unbalanced dynamic generic type description
alloc/free aborts it regardless of optimization. A library test's only token is `slow`: like `stress`, it takes
seconds to run and so is scheduled first, but it is not excluded from quick runs (valgrind runs do not include
library tests at all). See the top of `tests.py` for the full, current list.

A file that looks like a test but is missing a file its category requires, or that carries an unrecognized directive
token, fails the suite rather than being silently skipped, so check the test output if you add a file and the suite
does not seem to pick it up.

If you change discovery or scheduling in `tests.py` itself, run `ninja -C build testspy`, which checks them against
isolated fixtures instead of the real `tests/` tree.

## Commit message

It's a convention that every commit message begins with an emoji. This emojis doesn't have to have any deeper meaning, we just want Emojicode's GitHub page to look nice. Feel free to choose any you like, we advise you, however, to choose emojis wisely and with regard to what people might associate it with.

Otherwise, commit messages should concisely describe the change. The first line should be 50 characters or less. Start with an imperative verb. For instance, `🚨 Use parameter pack for CompilerError`.


