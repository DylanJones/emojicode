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

return M
