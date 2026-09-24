# Emojicode for Neovim

Filetype detection, comments, `%` between 🍇 and 🍉, and a configuration for the `emojicode-lsp` language server,
which reports errors as you type, shows types on hover (`K`), goes to definitions (`gd`) and highlights code.
Requires Neovim 0.11 or newer.

## Installing

Add this directory to your runtimepath, e.g. with lazy.nvim:

```lua
{ dir = '/path/to/emojicode/editors/nvim' }
```

or directly in `init.lua`:

```lua
vim.opt.runtimepath:prepend('/path/to/emojicode/editors/nvim')
```

Then enable the language server:

```lua
vim.lsp.enable('emojicode')
```

`emojicode-lsp` must be on your `PATH`. Otherwise, or to search more directories for packages, override the
configuration in `lsp/emojicode.lua`:

```lua
vim.lsp.config('emojicode', {
  cmd = { '/path/to/build/LanguageServer/emojicode-lsp' },
  init_options = { packageSearchPaths = { '/path/to/packages' } },
})
```

A development build of the server finds the packages in its build directory by itself.
