-- json.lua: small strict JSON decoder (GitHub API responses only).
local json = {}
json.null = setmetatable({}, { __tostring = function() return "null" end })

local function utf8char(cp)
  if cp < 0x80 then return string.char(cp)
  elseif cp < 0x800 then
    return string.char(0xC0 + math.floor(cp / 0x40), 0x80 + cp % 0x40)
  elseif cp < 0x10000 then
    return string.char(0xE0 + math.floor(cp / 0x1000),
      0x80 + math.floor(cp / 0x40) % 0x40, 0x80 + cp % 0x40)
  end
  return string.char(0xF0 + math.floor(cp / 0x40000),
    0x80 + math.floor(cp / 0x1000) % 0x40,
    0x80 + math.floor(cp / 0x40) % 0x40, 0x80 + cp % 0x40)
end

local ESC = { ['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f",
              n = "\n", r = "\r", t = "\t" }

function json.decode(s)
  local pos = 1
  local depth = 0

  local function fail(msg) error("json: " .. msg .. " at " .. pos, 0) end
  local function skip() pos = s:find("[^ \t\r\n]", pos) or (#s + 1) end

  local value

  local function str()
    local out, i = {}, pos + 1
    while true do
      local j = s:find('["\\]', i)
      if not j then fail("unterminated string") end
      out[#out + 1] = s:sub(i, j - 1)
      if s:sub(j, j) == '"' then pos = j + 1; return table.concat(out) end
      local e = s:sub(j + 1, j + 1)
      if e == "u" then
        local hex = s:match("^%x%x%x%x", j + 2)
        if not hex then fail("bad \\u escape") end
        local cp = tonumber(hex, 16)
        i = j + 6
        if cp >= 0xD800 and cp <= 0xDBFF then
          local lo = s:match("^\\u(%x%x%x%x)", i)
          if lo and tonumber(lo, 16) >= 0xDC00 and tonumber(lo, 16) <= 0xDFFF then
            cp = 0x10000 + (cp - 0xD800) * 0x400 + (tonumber(lo, 16) - 0xDC00)
            i = i + 6
          else
            cp = 0xFFFD
          end
        elseif cp >= 0xDC00 and cp <= 0xDFFF then
          cp = 0xFFFD
        end
        out[#out + 1] = utf8char(cp)
      else
        if not ESC[e] then fail("bad escape") end
        out[#out + 1] = ESC[e]
        i = j + 2
      end
    end
  end

  function value()
    skip()
    local c = s:sub(pos, pos)
    if c == "{" then
      depth = depth + 1
      if depth > 64 then fail("too deep") end
      local t = {}
      pos = pos + 1
      skip()
      if s:sub(pos, pos) == "}" then pos = pos + 1; depth = depth - 1; return t end
      while true do
        skip()
        if s:sub(pos, pos) ~= '"' then fail("expected key") end
        local k = str()
        skip()
        if s:sub(pos, pos) ~= ":" then fail("expected ':'") end
        pos = pos + 1
        t[k] = value()
        skip()
        local d = s:sub(pos, pos)
        pos = pos + 1
        if d == "}" then break end
        if d ~= "," then fail("expected ',' or '}'") end
      end
      depth = depth - 1
      return t
    elseif c == "[" then
      depth = depth + 1
      if depth > 64 then fail("too deep") end
      local t, n = {}, 0
      pos = pos + 1
      skip()
      if s:sub(pos, pos) == "]" then pos = pos + 1; depth = depth - 1; return t end
      while true do
        n = n + 1
        t[n] = value()
        skip()
        local d = s:sub(pos, pos)
        pos = pos + 1
        if d == "]" then break end
        if d ~= "," then fail("expected ',' or ']'") end
      end
      depth = depth - 1
      return t
    elseif c == '"' then
      return str()
    elseif s:sub(pos, pos + 3) == "true" then pos = pos + 4; return true
    elseif s:sub(pos, pos + 4) == "false" then pos = pos + 5; return false
    elseif s:sub(pos, pos + 3) == "null" then pos = pos + 4; return json.null
    else
      local num = s:match("^-?%d+%.?%d*[eE]?[+-]?%d*", pos)
      if not num or num == "" then fail("unexpected character") end
      pos = pos + #num
      return tonumber(num) or fail("bad number")
    end
  end

  local v = value()
  skip()
  if pos <= #s then fail("trailing data") end
  return v
end

-- Convenience: json.null -> nil for string fields.
function json.str(v)
  if type(v) == "string" then return v end
  return nil
end

return json
