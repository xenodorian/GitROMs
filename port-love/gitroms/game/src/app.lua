-- app.lua: GitROMs screens, navigation, GitHub browsing and downloads.
local cfg = require("src.config")
local json = require("src.json")
local net = require("src.net")
local fsu = require("src.fsutil")

local app = {}

local W, H = 640, 480
local COL = {
  bg = { 16, 18, 16 }, surface = { 24, 28, 24 }, raised = { 35, 41, 36 },
  fg = { 231, 239, 228 }, muted = { 168, 181, 166 }, line = { 49, 56, 50 },
  signal = { 230, 162, 60 }, ink = { 22, 19, 12 }, bad = { 224, 100, 90 },
  good = { 120, 200, 120 },
}
local function color(c, a)
  love.graphics.setColor(c[1] / 255, c[2] / 255, c[3] / 255, a or 1)
end

local fonts = {}
local S = {}           -- all mutable state
local cache = {}       -- key -> list of items, disposable
local home, logpath, histpath

---------------------------------------------------------------------------
-- logging
---------------------------------------------------------------------------
local function log(msg)
  if not logpath then return end
  local f = io.open(logpath, "ab")
  if f then
    f:write(os.date("%Y-%m-%d %H:%M:%S "), tostring(msg), "\n")
    f:close()
  end
end
app.log = log

---------------------------------------------------------------------------
-- text helpers
---------------------------------------------------------------------------
local function clean(s)
  s = tostring(s or "")
  s = s:gsub("[%c]", " ")
  local ok, utf8 = pcall(require, "utf8")
  if ok and utf8.len(s) then return s end
  return (s:gsub("[\128-\255]", "?"))
end

local function fit(font, s, width)
  s = clean(s)
  if font:getWidth(s) <= width then return s end
  local ok, utf8 = pcall(require, "utf8")
  local n = ok and utf8.len(s) or #s
  while n > 1 do
    n = n - 1
    local cut = ok and (utf8.offset(s, n + 1) - 1) or n
    local t = s:sub(1, cut) .. "..."
    if font:getWidth(t) <= width then return t end
  end
  return "..."
end

local function print_at(font, s, x, y, c, width)
  love.graphics.setFont(font)
  color(c or COL.fg)
  love.graphics.print(width and fit(font, s, width) or clean(s), x, y)
end

local function wrap_lines(font, s, width)
  local _, lines = font:getWrap(clean(s), width)
  return lines
end

---------------------------------------------------------------------------
-- history (small text file, no credentials)
---------------------------------------------------------------------------
local function load_history()
  S.history = {}
  local s = fsu.readall(histpath, 64 * 1024)
  if not s then return end
  for line in s:gmatch("[^\n]+") do
    local t, st, name, sys, msg = line:match("^(.-)\t(.-)\t(.-)\t(.-)\t(.*)$")
    if t then table.insert(S.history, 1, { time = t, status = st, name = name, system = sys, msg = msg }) end
  end
  while #S.history > 100 do table.remove(S.history) end
end

local function add_history(status, name, system, msg)
  table.insert(S.history, 1, { time = os.date("%m-%d %H:%M"), status = status, name = name, system = system, msg = msg or "" })
  while #S.history > 100 do table.remove(S.history) end
  local f = io.open(histpath, "ab")
  if f then
    f:write(string.format("%s\t%s\t%s\t%s\t%s\n", os.date("%m-%d %H:%M"), status,
      name:gsub("[\t\n]", " "), system, (msg or ""):gsub("[\t\n]", " ")))
    f:close()
  end
end

---------------------------------------------------------------------------
-- background tasks (coroutines that wait on curl jobs)
---------------------------------------------------------------------------
local function run_task(label, fn, on_done)
  local t = { label = label, cancelled = false }
  t.co = coroutine.create(function() return fn(t) end)
  t.on_done = on_done
  S.task = t
end

local function wait_job(t, job)
  t.job = job
  while true do
    if t.cancelled then
      net.cancel(job)
      t.job = nil
      return { cancelled = true }
    end
    local r = net.poll(job)
    if r then t.job = nil; return r end
    coroutine.yield()
  end
end

local function step_task()
  local t = S.task
  if not t then return end
  local ok, a, b = coroutine.resume(t.co)
  if not ok then
    log("task error: " .. tostring(a))
    S.task = nil
    t.on_done(nil, "Internal error. See gitroms.log.")
  elseif coroutine.status(t.co) == "dead" then
    S.task = nil
    t.on_done(a, b)
  end
end

local resp_seq = 0
local function fetch_json(t, url)
  resp_seq = resp_seq + 1
  local out = string.format("%s/resp%d.json", net.tmpdir(), resp_seq)
  log("GET " .. url)
  local r = wait_job(t, net.start(url, out, { max_time = 40 }))
  if r.cancelled then os.remove(out); return nil, "Cancelled." end
  if r.rc ~= 0 then os.remove(out); log("curl " .. r.rc .. ": " .. r.err); return nil, net.explain(r) end
  if fsu.filesize(out) > 4 * 1024 * 1024 then os.remove(out); return nil, "Response from GitHub was too large." end
  local body = fsu.readall(out) or ""
  os.remove(out)
  local ok, data = pcall(json.decode, body)
  if not ok then log("bad json: " .. tostring(data)); return nil, "GitHub sent a response I could not read." end
  return data
end

---------------------------------------------------------------------------
-- GitHub listing
---------------------------------------------------------------------------
local function visible_file(name)
  if name:sub(1, 1) == "." then return false end
  -- ".md" is also Markdown; Mega Drive ROMs are never called README/LICENSE.
  local low = name:lower()
  if low:match("^readme") or low:match("^license") or low:match("^changelog") then return false end
  return cfg.extensions[fsu.ext(name)] == true
end

local function list_repos(t)
  local repos = {}
  for page = 1, 10 do
    local url = string.format("%s/users/%s/repos?per_page=100&sort=updated&type=owner&page=%d", cfg.api, cfg.user, page)
    local data, err = fetch_json(t, url)
    if not data then return nil, err end
    if type(data) ~= "table" or data.message then
      return nil, "Unexpected reply from GitHub."
    end
    for _, r in ipairs(data) do
      if type(r) == "table" and type(r.name) == "string" then
        repos[#repos + 1] = {
          kind = "repo", name = clean(r.name), owner = (type(r.owner) == "table" and r.owner.login) or cfg.user,
          desc = clean(json.str(r.description) or ""), branch = json.str(r.default_branch) or "main",
        }
      end
    end
    if #data < 100 then break end
  end
  return repos
end

local function list_dir(t, repo, path)
  local url = string.format("%s/repos/%s/%s/contents%s?ref=%s", cfg.api, repo.owner, repo.name,
    path ~= "" and ("/" .. fsu.urlpath(path)) or "", fsu.urlpath(repo.branch))
  local data, err = fetch_json(t, url)
  if not data then
    if path == "" and err:find("404", 1, true) then err = "This repository is empty or has no files on its default branch." end
    return nil, err
  end
  if type(data) ~= "table" or data.message then return nil, "Unexpected reply from GitHub." end
  if data.type then data = { data } end
  local items = {}
  for _, e in ipairs(data) do
    if type(e) == "table" and type(e.name) == "string" then
      local name = clean(e.name)
      if e.type == "dir" then
        if name:sub(1, 1) ~= "." then
          items[#items + 1] = { kind = "dir", name = name, path = e.path or name }
        end
      elseif e.type == "file" and visible_file(e.name) and type(e.download_url) == "string" then
        items[#items + 1] = { kind = "file", name = name, rawname = e.name, path = e.path or name,
          size = tonumber(e.size) or 0, url = e.download_url }
      end
    end
  end
  table.sort(items, function(a, b)
    if a.kind ~= b.kind then return a.kind == "dir" end
    return a.name:lower() < b.name:lower()
  end)
  return items
end

---------------------------------------------------------------------------
-- navigation
---------------------------------------------------------------------------
local function frame_key(f)
  if f.kind == "repos" then return "repos:" .. cfg.user end
  return "dir:" .. f.repo.owner .. "/" .. f.repo.name .. "@" .. f.repo.branch .. ":" .. f.path
end

local function set_list(frame, items, keep_sel)
  S.items = items
  if not keep_sel then frame.sel, frame.scroll = 1, 0 end
  S.frame = frame
  S.screen = "list"
  S.status = nil
end

local function load_frame(frame, force, keep_sel, push)
  local key = frame_key(frame)
  local function show(items)
    -- Only remember the parent once the child actually loaded.
    if push and S.frame then table.insert(S.stack, S.frame) end
    set_list(frame, items, keep_sel)
  end
  if cache[key] and not force then
    show(cache[key])
    return
  end
  S.screen = "busy"
  S.busy_label = frame.kind == "repos" and ("Loading " .. cfg.user .. " repositories...") or ("Loading " .. frame.path .. "...")
  run_task("load", function(t)
    if frame.kind == "repos" then return list_repos(t) end
    return list_dir(t, frame.repo, frame.path)
  end, function(items, err)
    if items then
      cache[key] = items
      show(items)
    else
      S.screen = "error"
      S.error = err
      S.error_back = S.frame ~= nil
    end
  end)
end

local function push_and_load(frame)
  load_frame(frame, false, false, true)
end

local function go_back()
  local prev = table.remove(S.stack)
  if prev then
    S.frame = nil
    load_frame(prev, false, true)
  else
    S.screen = "exit"
  end
end

---------------------------------------------------------------------------
-- download
---------------------------------------------------------------------------
local function default_system(name)
  return cfg.ext_to_system[fsu.ext(name)] or "arcade"
end

local function open_confirm(item)
  local sys = default_system(item.name)
  local idx = 1
  for i, s in ipairs(cfg.systems) do if s == sys then idx = i end end
  S.confirm = { item = item, sys_idx = idx, name = fsu.sanitize(item.rawname or item.name) }
  S.screen = "confirm"
end

local function dest_dir(c)
  return S.romroot .. "/" .. cfg.systems[c.sys_idx]
end

local function begin_download(c, final_name)
  local dir = dest_dir(c)
  if not fsu.mkdir(dir) or not fsu.writable_dir(dir) then
    S.screen = "error"; S.error = "Cannot write to " .. dir; S.error_back = true
    return
  end
  local need = c.item.size
  if need > 0 then
    local free = fsu.free_bytes(dir)
    if free and free < need + 5 * 1024 * 1024 then
      S.screen = "error"; S.error = "Not enough free space. Need " .. fsu.human_size(need) .. ", have " .. fsu.human_size(free) .. "."
      S.error_back = true
      return
    end
  end
  local part = dir .. "/" .. final_name .. ".gitroms-part"
  local final = dir .. "/" .. final_name
  S.dl = { name = final_name, sys = cfg.systems[c.sys_idx], total = need, got = 0, part = part, t0 = os.time() }
  S.screen = "download"
  log("download " .. c.item.url .. " -> " .. final)
  run_task("download", function(t)
    os.remove(part)
    local r = wait_job(t, net.start(c.item.url, part, { max_time = 3600 }))
    if r.cancelled then os.remove(part); return nil, "Cancelled." end
    if r.rc ~= 0 then os.remove(part); log("curl " .. r.rc .. ": " .. r.err); return nil, net.explain(r) end
    local size = fsu.filesize(part)
    if size == 0 then os.remove(part); return nil, "The downloaded file was empty." end
    if fsu.exists(final) then os.remove(part); return nil, "A file with that name appeared. Not overwritten." end
    local ok, e = os.rename(part, final)
    if not ok then os.remove(part); return nil, "Could not save file: " .. tostring(e) end
    return { size = size, final = final }
  end, function(res, err)
    if res then
      add_history("OK", final_name, S.dl.sys, fsu.human_size(res.size))
      S.result = { ok = true, text = "Saved " .. final_name .. " (" .. fsu.human_size(res.size) .. ") to " .. S.dl.sys .. ".",
        note = (c.item.size > 0 and res.size ~= c.item.size) and "Size differs from the listing (possibly a Git LFS file)." or nil }
    else
      add_history("FAIL", final_name, S.dl.sys, err)
      S.result = { ok = false, text = err }
    end
    S.dl = nil
    S.screen = "result"
  end)
end

local function start_download(c)
  local dir = dest_dir(c)
  if fsu.exists(dir .. "/" .. c.name) then
    c.copy_name = fsu.unique_name(dir, c.name)
    S.conflict = { c = c, sel = 1 }
    S.screen = "conflict"
  else
    begin_download(c, c.name)
  end
end

---------------------------------------------------------------------------
-- input
---------------------------------------------------------------------------
local function visible_rows() return 12 end

local function move(delta)
  local f = S.frame
  if not f or #S.items == 0 then return end
  f.sel = math.max(1, math.min(#S.items, f.sel + delta))
  local rows = visible_rows()
  if f.sel <= f.scroll then f.scroll = f.sel - 1 end
  if f.sel > f.scroll + rows then f.scroll = f.sel - rows end
end

local function refresh()
  if S.frame then
    cache[frame_key(S.frame)] = nil
    load_frame(S.frame, true, true)
  end
end

function app.action(a)
  local scr = S.screen
  if scr == "busy" then
    if a == "cancel" and S.task then
      S.task.cancelled = true
    end
  elseif scr == "list" then
    if a == "up" then move(-1)
    elseif a == "down" then move(1)
    elseif a == "pgup" then move(-visible_rows())
    elseif a == "pgdn" then move(visible_rows())
    elseif a == "left" then move(-visible_rows())
    elseif a == "right" then move(visible_rows())
    elseif a == "cancel" then go_back()
    elseif a == "select" then refresh()
    elseif a == "start" then S.prev_screen = "list"; S.hist_scroll = 0; S.screen = "history"
    elseif a == "confirm" then
      local it = S.items[S.frame and S.frame.sel or 0]
      if not it then return end
      if it.kind == "repo" then
        push_and_load({ kind = "dir", repo = it, path = "" })
      elseif it.kind == "dir" then
        push_and_load({ kind = "dir", repo = S.frame.repo, path = it.path })
      elseif it.kind == "file" then
        if not S.romroot then
          S.screen = "error"; S.error = "ROM folder not found. Looked in: " .. table.concat(cfg.rom_roots, ", "); S.error_back = true
        else
          open_confirm(it)
        end
      end
    end
  elseif scr == "confirm" then
    local c = S.confirm
    if a == "left" then c.sys_idx = (c.sys_idx - 2) % #cfg.systems + 1
    elseif a == "right" then c.sys_idx = c.sys_idx % #cfg.systems + 1
    elseif a == "cancel" then S.screen = "list"
    elseif a == "confirm" then start_download(c) end
  elseif scr == "conflict" then
    local k = S.conflict
    if a == "up" or a == "down" then k.sel = 3 - k.sel
    elseif a == "cancel" then S.screen = "list"
    elseif a == "confirm" then
      if k.sel == 1 then begin_download(k.c, k.c.copy_name) else S.screen = "list" end
    end
  elseif scr == "download" then
    if a == "cancel" and S.task then S.task.cancelled = true end
  elseif scr == "result" then
    if a == "confirm" or a == "cancel" then S.screen = "list" end
  elseif scr == "error" then
    if a == "confirm" or a == "cancel" then
      if S.error_back and S.frame then S.screen = "list"
      elseif S.frame == nil and #S.stack > 0 then go_back()
      else
        S.error_back = nil
        S.stack = {}
        load_frame({ kind = "repos" }, true)
      end
    elseif a == "select" or a == "start" then
      S.stack = {}; S.frame = nil
      load_frame({ kind = "repos" }, true)
    end
  elseif scr == "history" then
    if a == "up" then S.hist_scroll = math.max(0, S.hist_scroll - 1)
    elseif a == "down" then S.hist_scroll = math.min(math.max(0, #S.history - 10), S.hist_scroll + 1)
    elseif a == "cancel" or a == "start" then S.screen = S.prev_screen or "list" end
  elseif scr == "exit" then
    if a == "confirm" then love.event.quit()
    elseif a == "cancel" then S.screen = "list" end
  end
end

local KEYS = {
  up = "up", down = "down", left = "left", right = "right",
  z = "confirm", space = "confirm", x = "cancel", escape = "cancel",
  ["return"] = "start", kpenter = "start", tab = "select", q = "pgup", e = "pgdn",
}
local PAD = {
  dpup = "up", dpdown = "down", dpleft = "left", dpright = "right",
  a = "confirm", b = "cancel", start = "start", back = "select",
  leftshoulder = "pgup", rightshoulder = "pgdn", y = "select",
}
local last_action = {}

local function fire(a, repeated)
  local now = love.timer.getTime()
  -- gptokeyb keys and gamepad events can both arrive for one press.
  if not repeated and last_action[a] and now - last_action[a] < 0.05 then return end
  last_action[a] = now
  app.action(a)
end

function app.key(key, isrepeat)
  local a = KEYS[key]
  if not a then return end
  if isrepeat and a ~= "up" and a ~= "down" then return end
  fire(a, isrepeat)
end

function app.pad(button)
  local a = PAD[button]
  if a then fire(a, false) end
end

---------------------------------------------------------------------------
-- lifecycle
---------------------------------------------------------------------------
function app.load()
  home = os.getenv("GITROMS_HOME")
  if not home or home == "" then home = love.filesystem.getSaveDirectory() end
  fsu.mkdir(home)
  logpath = home .. "/gitroms.log"
  histpath = home .. "/history.txt"
  fsu.writeall(logpath, "")
  log("GitROMs 2.0 start, home=" .. home)

  fonts.small = love.graphics.newFont(13)
  fonts.body = love.graphics.newFont(16)
  fonts.big = love.graphics.newFont(22)

  S = { stack = {}, items = {}, screen = "busy", history = {} }
  load_history()
  net.init(home .. "/tmp")

  local override = os.getenv("GITROMS_ROMS")
  local roots = override and { override } or cfg.rom_roots
  for _, r in ipairs(roots) do
    if fsu.writable_dir(r) then S.romroot = r; break end
  end
  log("rom root: " .. tostring(S.romroot))

  if not net.have_curl() then
    S.screen = "error"
    S.error = "curl was not found on this device, so GitROMs cannot reach GitHub."
    S.error_back = false
    S.fatal = true
    return
  end
  load_frame({ kind = "repos" })
end

function app.update(dt)
  step_task()
  local d = S.dl
  if d and S.screen == "download" then
    d.poll = (d.poll or 0) - dt
    if d.poll <= 0 then d.poll = 0.25; d.got = fsu.filesize(d.part) end
  end
end

function app.shutdown()
  net.cancel_all()
  if S.dl then os.remove(S.dl.part) end
  log("exit")
end

function app.state() return S end

---------------------------------------------------------------------------
-- drawing
---------------------------------------------------------------------------
local function header(title, right)
  color(COL.surface); love.graphics.rectangle("fill", 0, 0, W, 48)
  color(COL.signal); love.graphics.rectangle("fill", 0, 46, W, 2)
  print_at(fonts.big, title, 16, 10, COL.fg, W - 32 - (right and 140 or 0))
  if right then
    love.graphics.setFont(fonts.small)
    color(COL.muted)
    love.graphics.printf(clean(right), W - 156, 17, 140, "right")
  end
end

local function footer(text)
  color(COL.surface); love.graphics.rectangle("fill", 0, H - 36, W, 36)
  color(COL.line); love.graphics.rectangle("fill", 0, H - 36, W, 1)
  print_at(fonts.small, text, 16, H - 26, COL.muted, W - 32)
end

local function panel_text(lines, y, font, c)
  font = font or fonts.body
  love.graphics.setFont(font)
  color(c or COL.fg)
  for _, ln in ipairs(lines) do
    love.graphics.printf(ln, 32, y, W - 64, "left")
    y = y + font:getHeight() + 4
  end
  return y
end

local function progress_bar(x, y, w, frac)
  color(COL.raised); love.graphics.rectangle("fill", x, y, w, 18, 4, 4)
  if frac then
    color(COL.signal); love.graphics.rectangle("fill", x, y, math.max(4, w * math.min(1, frac)), 18, 4, 4)
  else
    local t = love.timer.getTime() * 1.2 % 1
    color(COL.signal); love.graphics.rectangle("fill", x + (w - 80) * t, y, 80, 18, 4, 4)
  end
end

local function draw_list()
  local f = S.frame
  local title, right
  if f.kind == "repos" then
    title = cfg.user; right = #S.items .. " repos"
  else
    title = f.repo.name .. (f.path ~= "" and ("/" .. f.path) or "")
    right = f.repo.branch
  end
  header(title, right)
  if #S.items == 0 then
    local msg = f.kind == "repos" and "No public repositories found." or "No ROM files or folders here."
    panel_text({ msg }, 120, fonts.body, COL.muted)
  end
  local rows = visible_rows()
  for i = 1, rows do
    local idx = f.scroll + i
    local it = S.items[idx]
    if not it then break end
    local y = 56 + (i - 1) * 30
    if idx == f.sel then
      color(COL.signal); love.graphics.rectangle("fill", 8, y, W - 16, 28, 4, 4)
    end
    local c = (idx == f.sel) and COL.ink or COL.fg
    local sub = (idx == f.sel) and COL.ink or COL.muted
    if it.kind == "repo" then
      print_at(fonts.body, it.name, 16, y + 5, c, 230)
      print_at(fonts.small, it.desc, 256, y + 8, sub, W - 272)
    elseif it.kind == "dir" then
      print_at(fonts.body, it.name .. "/", 16, y + 5, c, W - 40)
    else
      print_at(fonts.body, it.name, 16, y + 5, c, W - 150)
      love.graphics.setFont(fonts.small); color(sub)
      love.graphics.printf(fsu.human_size(it.size), W - 124, y + 8, 108, "right")
    end
  end
  if #S.items > rows then
    local trackh = rows * 30
    color(COL.line); love.graphics.rectangle("fill", W - 6, 56, 3, trackh)
    color(COL.muted)
    local bh = math.max(16, trackh * rows / #S.items)
    love.graphics.rectangle("fill", W - 6, 56 + (trackh - bh) * f.scroll / (#S.items - rows), 3, bh)
  end
  footer("A open   B back   Select refresh   Start downloads   L/R page")
end

local function draw_confirm()
  local c = S.confirm
  header("Download")
  local y = panel_text(wrap_lines(fonts.big, c.name, W - 64), 70, fonts.big, COL.fg)
  y = y + 6
  local rows = {
    { "Size", c.item.size > 0 and fsu.human_size(c.item.size) or "unknown" },
    { "From", S.frame.repo.name .. "/" .. c.item.path },
    { "Save to", S.romroot .. "/" },
  }
  for _, r in ipairs(rows) do
    print_at(fonts.body, r[1], 32, y, COL.muted)
    print_at(fonts.body, r[2], 130, y, COL.fg, W - 160)
    y = y + 26
  end
  y = y + 10
  print_at(fonts.body, "System folder", 32, y, COL.muted)
  color(COL.raised); love.graphics.rectangle("fill", 160, y - 6, 280, 34, 6, 6)
  print_at(fonts.big, "<  " .. cfg.systems[c.sys_idx] .. "  >", 180, y - 2, COL.signal, 250)
  footer("A download   B cancel   Left/Right change system folder")
end

local function draw_conflict()
  local k = S.conflict
  header("File already exists")
  local y = panel_text({ k.c.name, "is already in " .. cfg.systems[k.c.sys_idx] .. ". It will not be overwritten." }, 70, fonts.body)
  local opts = { "Save as  " .. k.c.copy_name, "Keep existing file (cancel)" }
  y = y + 20
  for i, o in ipairs(opts) do
    if i == k.sel then color(COL.signal); love.graphics.rectangle("fill", 24, y, W - 48, 34, 6, 6) end
    print_at(fonts.body, o, 36, y + 8, i == k.sel and COL.ink or COL.fg, W - 72)
    y = y + 42
  end
  footer("A choose   B cancel   Up/Down move")
end

local function draw_download()
  local d = S.dl
  header("Downloading")
  local y = panel_text({ d.name }, 80, fonts.body)
  local frac = d.total > 0 and (d.got / d.total) or nil
  if frac and frac > 1 then frac = nil end
  progress_bar(32, y + 20, W - 64, frac)
  local info = fsu.human_size(d.got) .. (d.total > 0 and (" of " .. fsu.human_size(d.total)) or "")
  print_at(fonts.body, info, 32, y + 50, COL.muted)
  print_at(fonts.small, "Saving to " .. d.sys, 32, y + 80, COL.muted)
  footer("B cancel download")
end

local function draw_message(title, lines, c, hint)
  header(title)
  panel_text(lines, 90, fonts.body, c)
  footer(hint)
end

local function draw_history()
  header("Downloads", #S.history .. " saved")
  if #S.history == 0 then panel_text({ "Nothing downloaded yet." }, 90, fonts.body, COL.muted) end
  for i = 1, 10 do
    local h = S.history[S.hist_scroll + i]
    if not h then break end
    local y = 58 + (i - 1) * 40
    color(COL.surface); love.graphics.rectangle("fill", 8, y, W - 16, 36, 4, 4)
    print_at(fonts.body, h.name, 16, y + 2, h.status == "OK" and COL.fg or COL.bad, W - 40)
    print_at(fonts.small, h.time .. "   " .. h.system .. "   " .. h.status .. "   " .. h.msg, 16, y + 20, COL.muted, W - 40)
  end
  footer("Up/Down scroll   B back")
end

function app.draw()
  color(COL.bg)
  love.graphics.rectangle("fill", 0, 0, W, H)
  local s = S.screen
  if s == "list" then draw_list()
  elseif s == "busy" then
    header("GitROMs")
    panel_text({ S.busy_label or "Loading..." }, 150, fonts.big)
    progress_bar(120, 210, W - 240, nil)
    footer("B cancel")
  elseif s == "confirm" then draw_confirm()
  elseif s == "conflict" then draw_conflict()
  elseif s == "download" then draw_download()
  elseif s == "result" then
    local r = S.result
    local lines = { r.text }
    if r.note then lines[#lines + 1] = r.note end
    draw_message(r.ok and "Done" or "Download failed", lines, r.ok and COL.good or COL.bad, "A / B continue")
  elseif s == "error" then
    draw_message("Problem", { S.error or "Unknown error." }, COL.bad,
      S.fatal and "Press Select + Start to quit" or "A / B back   Start retry")
  elseif s == "history" then draw_history()
  elseif s == "exit" then
    draw_message("Quit GitROMs?", { "Press A to quit, B to stay." }, COL.fg, "A quit   B stay")
  end
end

return app
