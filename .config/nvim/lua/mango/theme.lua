local M = {}
local function channels(hex)
  return tonumber(hex:sub(2, 3), 16), tonumber(hex:sub(4, 5), 16), tonumber(hex:sub(6, 7), 16)
end

local function luminance(hex)
  local red, green, blue = channels(hex)
  local function linear(value)
    value = value / 255
    return value <= 0.04045 and value / 12.92 or ((value + 0.055) / 1.055) ^ 2.4
  end
  return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue)
end

local function contrast(first, second)
  local high, low = luminance(first), luminance(second)
  if high < low then
    high, low = low, high
  end
  return (high + 0.05) / (low + 0.05)
end

local function readable(color, background, target)
  if contrast(color, background) >= target then
    return color
  end
  local red, green, blue = channels(color)
  local destination = luminance(background) > 0.5 and 0 or 255
  for step = 1, 20 do
    local amount = step / 20
    local candidate = string.format(
      "#%02x%02x%02x",
      math.floor(red + (destination - red) * amount + 0.5),
      math.floor(green + (destination - green) * amount + 0.5),
      math.floor(blue + (destination - blue) * amount + 0.5)
    )
    if contrast(candidate, background) >= target then
      return candidate
    end
  end
  return destination == 0 and "#000000" or "#ffffff"
end
local transparent_groups = {
  "Normal",
  "NormalNC",
  "EndOfBuffer",
  "SignColumn",
  "FoldColumn",
}

local surface_groups = {
  "NormalFloat",
  "WinBar",
  "WinBarNC",
  "NeoTreeNormal",
  "NeoTreeNormalNC",
  "NeoTreeEndOfBuffer",
  "NvimTreeNormal",
  "NvimTreeNormalNC",
  "NvimTreeEndOfBuffer",
  "SnacksNormal",
  "SnacksNormalNC",
  "SnacksDashboardNormal",
  "SnacksPicker",
  "SnacksPickerBox",
  "SnacksPickerInput",
  "SnacksPickerList",
  "SnacksPickerPreview",
  "TelescopeNormal",
  "TelescopePromptNormal",
  "TelescopeResultsNormal",
  "TelescopePreviewNormal",
}

local function apply_transparency(c)
  for _, name in ipairs(transparent_groups) do
    vim.api.nvim_set_hl(0, name, { fg = c.foreground, bg = "NONE" })
  end
  for _, name in ipairs(surface_groups) do
    vim.api.nvim_set_hl(0, name, { fg = c.foreground, bg = "NONE" })
  end
  for _, name in ipairs({
    "FloatBorder",
    "SnacksPickerBorder",
    "SnacksPickerInputBorder",
    "SnacksPickerListBorder",
    "SnacksPickerPreviewBorder",
    "TelescopeBorder",
    "TelescopePromptBorder",
    "TelescopeResultsBorder",
    "TelescopePreviewBorder",
  }) do
    vim.api.nvim_set_hl(0, name, { fg = c.outline, bg = "NONE" })
  end
  local primary = readable(c.mode == "dark" and c.bright_foreground or c.foreground, c.background, 7)
  local secondary = readable(c.muted, c.background, 5.5)
  for _, name in ipairs({
    "NonText",
    "Conceal",
    "SnacksPickerPathHidden",
    "SnacksPickerPathIgnored",
    "SnacksPickerDimmed",
    "SnacksPickerGitStatusIgnored",
  }) do
    vim.api.nvim_set_hl(0, name, { fg = secondary, bg = "NONE" })
  end
  for _, name in ipairs({
    "Directory",
    "SnacksPickerDirectory",
    "SnacksPickerDir",
    "SnacksPickerFile",
    "SnacksPickerGitStatusUntracked",
  }) do
    vim.api.nvim_set_hl(0, name, { fg = primary, bg = "NONE", bold = true })
  end
end

local palette_watcher
local reload_generation = 0

local function watch_palette()
  if palette_watcher then
    return
  end
  local uv = vim.uv or vim.loop
  palette_watcher = uv.new_fs_event()
  if not palette_watcher then
    return
  end
  local directory = vim.fn.stdpath("config") .. "/lua/mango"
  palette_watcher:start(directory, {}, function(error, filename)
    if error or (filename and filename ~= "palette.lua") then
      return
    end
    reload_generation = reload_generation + 1
    local generation = reload_generation
    vim.defer_fn(function()
      if generation ~= reload_generation or (vim.v.exiting ~= vim.NIL and vim.v.exiting ~= 0) then
        return
      end
      package.loaded["mango.palette"] = nil
      local ok, message = pcall(vim.cmd.colorscheme, "mango")
      if not ok then
        vim.notify("Mango theme reload failed: " .. message, vim.log.levels.ERROR)
      end
    end, 80)
  end)
  vim.api.nvim_create_autocmd("VimLeavePre", {
    group = vim.api.nvim_create_augroup("MangoPaletteWatcher", { clear = true }),
    once = true,
    callback = function()
      if palette_watcher then
        palette_watcher:stop()
        palette_watcher:close()
        palette_watcher = nil
      end
    end,
  })
end

function M.apply()
  local c = require("mango.palette")
  vim.cmd("highlight clear")
  vim.o.termguicolors = true
  vim.o.background = c.mode == "light" and "light" or "dark"
  vim.g.colors_name = "mango"
  local set = vim.api.nvim_set_hl
  local source = c.mode == "dark" and {
    red = c.bright_red,
    green = c.bright_green,
    yellow = c.bright_yellow,
    blue = c.bright_blue,
    magenta = c.bright_magenta,
    cyan = c.bright_cyan,
    orange = c.bright_yellow,
  } or c
  local syntax = {}
  for _, name in ipairs({ "red", "green", "yellow", "blue", "magenta", "cyan", "orange" }) do
    syntax[name] = readable(source[name], c.background, 5.5)
  end
  local groups = {
    Normal = { fg = readable(c.foreground, c.background, 7), bg = "NONE" },
    NormalFloat = { fg = readable(c.foreground, c.background, 7), bg = "NONE" },
    FloatBorder = { fg = readable(c.outline, c.background, 4.5), bg = "NONE" },
    CursorLine = { bg = "NONE" },
    CursorLineNr = { fg = readable(c.accent, c.background, 5.5), bold = true },
    LineNr = { fg = readable(c.outline, c.background, 4.5) },
    Visual = { fg = c.selection_fg, bg = c.selection },
    Search = { fg = c.accent_fg, bg = c.accent },
    IncSearch = { fg = c.background, bg = c.yellow },
    Comment = { fg = readable(c.mode == "dark" and c.bright_foreground or c.muted, c.background, 5.5), italic = true },
    Constant = { fg = syntax.orange },
    String = { fg = syntax.green },
    Character = { fg = syntax.green },
    Number = { fg = syntax.orange },
    Boolean = { fg = syntax.orange },
    Identifier = { fg = syntax.cyan },
    Function = { fg = syntax.blue },
    Statement = { fg = syntax.magenta },
    Keyword = { fg = syntax.magenta, italic = true },
    Operator = { fg = syntax.cyan },
    PreProc = { fg = syntax.yellow },
    Type = { fg = syntax.yellow },
    Special = { fg = syntax.cyan },
    Error = { fg = syntax.red, bold = true },
    WarningMsg = { fg = syntax.yellow },
    DiagnosticError = { fg = syntax.red },
    DiagnosticWarn = { fg = syntax.yellow },
    DiagnosticInfo = { fg = syntax.blue },
    DiagnosticHint = { fg = syntax.cyan },
    DiffAdd = { fg = syntax.green, bg = "NONE" },
    DiffChange = { fg = syntax.yellow, bg = "NONE" },
    DiffDelete = { fg = syntax.red, bg = "NONE" },
    Pmenu = { fg = c.foreground, bg = "NONE" },
    PmenuSel = { fg = c.selection_fg, bg = c.selection },
    StatusLine = { fg = c.foreground, bg = "NONE" },
    StatusLineNC = { fg = c.muted, bg = "NONE" },
    TabLineSel = { fg = c.accent, bg = "NONE", bold = true },
    WinSeparator = { fg = c.border },
  }
  for name, opts in pairs(groups) do
    set(0, name, opts)
  end
  apply_transparency(c)
  local group = vim.api.nvim_create_augroup("MangoTransparentBackground", { clear = true })
  vim.api.nvim_create_autocmd("User", {
    group = group,
    pattern = "VeryLazy",
    callback = function()
      apply_transparency(require("mango.palette"))
    end,
  })
  watch_palette()
end

return M
