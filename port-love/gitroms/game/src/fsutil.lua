-- fsutil.lua: file and path helpers using only io/os (no extra libraries).
local fsu = {}

function fsu.shq(s)
  return "'" .. tostring(s):gsub("'", "'\\''") .. "'"
end

local function ok(r)
  return r == 0 or r == true
end

function fsu.sh(cmd)
  return ok(os.execute(cmd))
end

function fsu.isdir(p)
  return fsu.sh("[ -d " .. fsu.shq(p) .. " ]")
end

function fsu.writable_dir(p)
  return fsu.sh("[ -d " .. fsu.shq(p) .. " ] && [ -w " .. fsu.shq(p) .. " ]")
end

function fsu.mkdir(p)
  return fsu.sh("mkdir -p " .. fsu.shq(p))
end

function fsu.exists(p)
  local f = io.open(p, "rb")
  if f then f:close(); return true end
  return false
end

function fsu.filesize(p)
  local f = io.open(p, "rb")
  if not f then return 0 end
  local n = f:seek("end") or 0
  f:close()
  return n
end

function fsu.readall(p, max)
  local f = io.open(p, "rb")
  if not f then return nil end
  local s = f:read(max or "*a")
  f:close()
  return s
end

function fsu.writeall(p, s)
  local f = io.open(p, "wb")
  if not f then return false end
  f:write(s)
  f:close()
  return true
end

-- Free space in bytes for the filesystem holding dir, or nil if unknown.
function fsu.free_bytes(dir)
  local h = io.popen("df -Pk " .. fsu.shq(dir) .. " 2>/dev/null")
  if not h then return nil end
  local out = h:read("*a") or ""
  h:close()
  local last = out:match("([^\n]+)\n?$")
  if not last then return nil end
  local avail = last:match("^%S+%s+%d+%s+%d+%s+(%d+)")
  return avail and tonumber(avail) * 1024 or nil
end

-- Remove path separators, control characters and leading dots. Ordinary
-- names (spaces, brackets, parentheses) are kept as they are.
function fsu.sanitize(name)
  name = tostring(name or ""):gsub("[%c]", ""):gsub("[/\\]", "_")
  name = name:gsub("^[%s%.]+", ""):gsub("%s+$", "")
  if name == "" then name = "download" end
  if #name > 200 then
    local base, ext = name:match("^(.*)(%.[^%.]*)$")
    if base and #ext < 16 then name = base:sub(1, 200 - #ext) .. ext
    else name = name:sub(1, 200) end
  end
  return name
end

function fsu.ext(name)
  local e = name:match("(%.[^%./]+)$")
  return e and e:lower() or ""
end

-- name -> "name (1).ext" style non-conflicting file name inside dir.
function fsu.unique_name(dir, name)
  if not fsu.exists(dir .. "/" .. name) then return name end
  local base, ext = name:match("^(.*)(%.[^%.]*)$")
  if not base then base, ext = name, "" end
  for i = 1, 999 do
    local cand = string.format("%s (%d)%s", base, i, ext)
    if not fsu.exists(dir .. "/" .. cand) then return cand end
  end
  return string.format("%s (%d)%s", base, os.time(), ext)
end

function fsu.human_size(n)
  n = tonumber(n) or 0
  if n >= 1024 * 1024 * 1024 then return string.format("%.1f GB", n / 1073741824) end
  if n >= 1024 * 1024 then return string.format("%.1f MB", n / 1048576) end
  if n >= 1024 then return string.format("%.0f KB", n / 1024) end
  return string.format("%d B", n)
end

-- Percent-encode a path for use in a URL, keeping "/" separators.
function fsu.urlpath(p)
  return (p:gsub("[^%w%-%._~/]", function(c) return string.format("%%%02X", c:byte()) end))
end

return fsu
