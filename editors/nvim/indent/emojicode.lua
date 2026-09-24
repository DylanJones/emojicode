if vim.b.did_indent then
  return
end
vim.b.did_indent = true

vim.bo.indentexpr = "v:lua.require'emojicode'.indent()"
vim.bo.indentkeys = '0{,0},0),0],!^F,o,O,e'
-- Typing 🍉 or 🍆 at the start of a line dedents it. indentkeys cannot express this for multibyte characters.
local group = vim.api.nvim_create_augroup('emojicode_indent_' .. vim.api.nvim_get_current_buf(), { clear = true })
vim.api.nvim_create_autocmd('TextChangedI', {
  group = group,
  buffer = 0,
  callback = function() require('emojicode').reindent_closing() end,
})
vim.b.undo_indent = 'setlocal indentexpr< indentkeys< | lua pcall(vim.api.nvim_del_augroup_by_name, "'
  .. 'emojicode_indent_' .. vim.api.nvim_get_current_buf() .. '")'
