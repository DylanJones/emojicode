vim.bo.commentstring = '💭 %s'
vim.bo.comments = ':💭'
vim.bo.expandtab = true
vim.bo.shiftwidth = 2
vim.bo.softtabstop = 2
-- 🍇 and 🍉 open and close blocks, so % jumps between them.
vim.bo.matchpairs = vim.bo.matchpairs .. ',🍇:🍉,🤜:🤛'

vim.b.undo_ftplugin = 'setlocal commentstring< comments< expandtab< shiftwidth< softtabstop< matchpairs<'
