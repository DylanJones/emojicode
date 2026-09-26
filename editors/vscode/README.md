# Emojicode for Visual Studio Code

Highlighting, bracket matching, folding and comment toggling for Emojicode. Like braces in other languages, typing
🍇, 🤜, 🐚 or 🍿 in code adds the closing 🍉, 🤛 or 🍆, and typing 🔤, 📗 or 📘 adds the one that ends the string or
documentation comment. Nothing is added in strings and comments. With the `emojicode-lsp` language server it also:

- shows errors and warnings as you type,
- shows types, method signatures and documentation on hover,
- goes to definitions (F12), also into the standard library,
- lists the types and methods of a file in the outline,
- highlights types, methods and variables by what they are,
- completes what can be written where the cursor is, e.g. methods and instance variables at the start of a line in
  a type, and types after them. Type a word and pick what it describes: `grapes` inserts 🍇, `append` finds the
  methods whose documentation mentions it, `class` or `if` insert the construct, and variable names complete as
  usual.

## Learner docs

For learning the language, turn on **Emojicode: Toggle Learner Docs** (or click *Learner docs* in the status bar).
Hovering over code then links to the section of the documentation that explains it: 🔂 to *For In*, ❗️ to
*Methods* where it declares a method and to *Calling Methods* where it calls one, and a 😀 in 🔤…🔤 to *String
Literals*, since it is just text there. With the language server, methods and types of packages such as 😀 or 🔢 link
to their page in the package documentation. **Emojicode: Open Documentation for Token at Cursor** opens the link
directly.

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
- `emojicode.learnerDocs.enabled`: link code to the documentation on hover (default off).
- `emojicode.learnerDocs.baseUrl`: the documentation website (default `https://www.emojicode.org/`). Set it to
  `http://localhost:8080/` to use the documentation's `./serve`.

## Developing

The grammar is generated: edit `syntaxes/generate.py` and run it, then `npm test`. The test tokenizes snippets with
the same engine as VS Code and checks that every Emojicode file in the repository tokenizes without a runaway
string or comment. `test/learnerDocs.test.js` checks which section the learner docs link each token to, and that
the headings exist if the documentation's repository is next to this one. `npm run test:integration` types into VS
Code, with a fresh profile, to check that 🍇 and the other openings are closed. To try the extension, open this
folder in VS Code and press F5.
