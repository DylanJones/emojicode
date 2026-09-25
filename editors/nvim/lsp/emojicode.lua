-- Configuration for the Emojicode language server, found by vim.lsp.enable('emojicode') when this directory is on
-- the runtimepath. Override any field with vim.lsp.config('emojicode', { ... }).
return {
  cmd = { 'emojicode-lsp' },
  filetypes = { 'emojicode' },
  root_markers = { '.git' },
  init_options = {
    -- More directories to search for packages, like the compiler's -S option.
    packageSearchPaths = {},
  },
}
