# Emojicode for Visual Studio Code

Highlighting, bracket matching, folding and comment toggling for Emojicode. With the `emojicode-lsp` language
server it also shows errors as you type, types on hover, goes to definitions and completes code, including emoji
by name.

## Installing

```bash
npm install
npm run compile
npx vsce package
code --install-extension emojicode-0.1.0.vsix
```

Build `emojicode-lsp` with the rest of Emojicode and put it on your `PATH`, or point `emojicode.server.path` to
it. Without it the extension still highlights code.

## Settings

- `emojicode.server.path`: the language server executable (default `emojicode-lsp`).
- `emojicode.packageSearchPaths`: more directories to search for packages, like the compiler's `-S`.
- `emojicode.trace.server`: log the messages between VS Code and the server.

## Developing

The grammar is generated: edit `syntaxes/generate.py` and run it, then `npm test`. The test tokenizes snippets with
the same engine as VS Code and checks that every Emojicode file in the repository tokenizes without a runaway
string or comment. To try the extension, open this folder in VS Code and press F5.
