# tree-sitter-emojicode

A [tree-sitter](https://tree-sitter.github.io) grammar for Emojicode, with queries for highlighting, folding,
indentation, text objects and local scopes. Neovim uses it through `editors/nvim`.

The grammar is ported from `docs/grammar.ebnf` and reads the lexer's emoji, keyword and whitespace sets from it. It
is meant for editors, so it is more permissive than the compiler, but it parses everything the compiler accepts:

```bash
python3 tools/grammar_check.py --build build --tree-sitter --mutations 5 --generate 1000
```

checks this for every file in the repository, for mutated copies of them and for documents generated from the
EBNF grammar (`ninja grammar` includes the check when the tree-sitter CLI is installed).

## Building

The parser in `src/` is generated and not committed, because `grammar.js` reads `docs/grammar.ebnf` and only works
in this repository. Generate it and build a shared library with the
[tree-sitter CLI](https://github.com/tree-sitter/tree-sitter/tree/master/cli):

```bash
tree-sitter generate
tree-sitter build
```

## Developing

After changing `grammar.js`, run `tree-sitter generate` and `tree-sitter test`.

`test/corpus` has the expected trees of a few examples; `tree-sitter test --update` rewrites them after an
intended change.
