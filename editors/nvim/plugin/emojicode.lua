-- Registered from plugin/ rather than ftdetect/, so that it also works when this directory is added to the
-- runtimepath after startup has scanned ftdetect/.
vim.filetype.add({
  extension = {
    emojic = 'emojicode',
    ['🍇'] = 'emojicode',
    emojii = 'emojicode',
  },
  filename = {
    ['🏛'] = 'emojicode',
  },
})
