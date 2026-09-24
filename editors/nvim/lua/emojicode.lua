local M = {}

--- Removes strings and comments from a line, so that the emoji in them are not counted.
local function code(line)
  line = line:gsub('🔤.-🔤', '')
  return (line:gsub('💭.*$', ''))
end

local function count(text, pattern)
  local _, n = text:gsub(pattern, '')
  return n
end

--- The indentation of line `lnum`: one level deeper than the previous line if that line opened more blocks (🍇) or
--- collections (🍿) than it closed, and one level shallower if this line starts by closing one (🍉 or 🍆).
function M.indent(lnum)
  lnum = lnum or vim.v.lnum
  local previous = vim.fn.prevnonblank(lnum - 1)
  if previous == 0 then
    return 0
  end
  local indent = vim.fn.indent(previous)
  local width = vim.fn.shiftwidth()
  local line = code(vim.fn.getline(previous))
  local opened = count(line, '🍇') + count(line, '🍿') - count(line, '🍉') - count(line, '🍆')
  -- A line that starts by closing was already dedented; what it opens after that counts.
  if line:match('^%s*🍉') or line:match('^%s*🍆') then
    opened = opened + 1
  end
  if opened > 0 then
    indent = indent + width
  end
  local current = vim.fn.getline(lnum)
  if current:match('^%s*🍉') or current:match('^%s*🍆') then
    indent = indent - width
  end
  return math.max(indent, 0)
end

--- Re-indents the current line in insert mode if 🍉 or 🍆 was just typed as its first character. indentkeys cannot
--- do this, as Neovim compares only the last byte of a multibyte key with it.
function M.reindent_closing()
  local row, col = unpack(vim.api.nvim_win_get_cursor(0))
  local line = vim.api.nvim_get_current_line()
  -- The text before the cursor, without a U+FE0F after the emoji.
  local before = line:sub(1, col):gsub('\239\184\143$', '')
  if not (before:match('^%s*🍉$') or before:match('^%s*🍆$')) then
    return
  end
  local current = line:match('^%s*')
  local width = M.indent(row)
  local indent
  if vim.bo.expandtab then
    indent = string.rep(' ', width)
  else
    local tabstop = vim.bo.tabstop
    indent = string.rep('\t', math.floor(width / tabstop)) .. string.rep(' ', width % tabstop)
  end
  if indent ~= current then
    vim.api.nvim_set_current_line(indent .. line:sub(#current + 1))
    vim.api.nvim_win_set_cursor(0, { row, col - #current + #indent })
  end
end

return M
