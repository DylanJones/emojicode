# Emojicode for Neovim

Filetype detection, comments, `%` between 🍇 and 🍉, and a configuration for the `emojicode-lsp` language server,
which reports errors as you type, highlights code, shows types and documentation on hover (`K`), goes to
definitions (`gd`), lists symbols (`gO`) and completes code: type a word like `grapes`, `append` or `class` and
pick the emoji, method or construct it describes. Requires Neovim 0.11 or newer.

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

For completion as you type with Neovim's built-in completion, enable it when the server attaches:

```lua
vim.api.nvim_create_autocmd('LspAttach', {
  callback = function(args)
    local client = vim.lsp.get_client_by_id(args.data.client_id)
    if client and client.name == 'emojicode' then
      vim.lsp.completion.enable(true, client.id, args.buf, { autotrigger = true })
    end
  end,
})
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
