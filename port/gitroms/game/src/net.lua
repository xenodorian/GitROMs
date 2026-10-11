-- net.lua: non-blocking HTTPS through the system curl.
-- LOVE has no built-in HTTPS, and ArkOS ships curl with its CA certificates,
-- so each request is a background curl process. The UI polls a small result
-- file, so it never blocks. No browser, no login, no tokens.
local fsu = require("src.fsutil")
local cfg = require("src.config")

local net = {}
local tmpdir
local seq = 0
local active = {}

function net.init(dir)
  tmpdir = dir
  fsu.mkdir(tmpdir)
  -- Remove only our own leftovers from an earlier run.
  fsu.sh("rm -f " .. fsu.shq(tmpdir) .. "/job*.rc " .. fsu.shq(tmpdir) .. "/job*.err "
    .. fsu.shq(tmpdir) .. "/job*.pid " .. fsu.shq(tmpdir) .. "/resp*.json")
end

function net.tmpdir() return tmpdir end

function net.have_curl()
  return fsu.sh("command -v curl >/dev/null 2>&1")
end

-- Start a download of url into outpath. Returns a job handle.
function net.start(url, outpath, opts)
  opts = opts or {}
  seq = seq + 1
  local base = string.format("%s/job%d", tmpdir, seq)
  local job = { rc = base .. ".rc", err = base .. ".err", pid = base .. ".pid",
                out = outpath, url = url }
  os.remove(job.rc); os.remove(job.err); os.remove(job.pid)
  local script = table.concat({
    "curl -sS -L -f --connect-timeout 15 --max-time ", tostring(opts.max_time or 60),
    " --retry 2 --retry-delay 2",
    " -A ", fsu.shq(cfg.user_agent),
    " -H ", fsu.shq("Accept: application/vnd.github+json"),
    " -o ", fsu.shq(outpath),
    " ", fsu.shq(url),
    " 2>", fsu.shq(job.err), " & CP=$!;",
    " trap 'kill $CP 2>/dev/null' TERM INT;",
    " wait $CP; echo $? >", fsu.shq(job.rc),
  })
  os.execute("sh -c " .. fsu.shq(script) .. " >/dev/null 2>&1 & echo $! >" .. fsu.shq(job.pid))
  active[job] = true
  return job
end

-- Returns nil while running, or { rc = <curl exit code>, err = <stderr text> }.
function net.poll(job)
  local rc = fsu.readall(job.rc, 16)
  if not rc or not rc:match("%d") then return nil end
  local err = fsu.readall(job.err, 400) or ""
  os.remove(job.rc); os.remove(job.err); os.remove(job.pid)
  active[job] = nil
  return { rc = tonumber(rc:match("%d+")), err = err:gsub("%s+$", "") }
end

function net.cancel(job)
  local pid = fsu.readall(job.pid, 16)
  if pid and pid:match("%d") then
    os.execute("kill " .. pid:match("%d+") .. " 2>/dev/null")
  end
  active[job] = nil
end

function net.cancel_all()
  for job in pairs(active) do net.cancel(job) end
end

-- Turn a curl result into a short message for the screen.
function net.explain(r)
  local code = r.err:match("error: (%d%d%d)")
  if code == "403" or code == "429" then
    return "GitHub refused the request (" .. code .. "). Anonymous limit is 60 per hour. Try again later."
  elseif code == "404" then
    return "Not found on GitHub (404)."
  elseif code then
    return "GitHub returned error " .. code .. "."
  end
  local m = {
    [6] = "Cannot find github.com. Is Wi-Fi connected?",
    [7] = "Cannot connect. Is Wi-Fi connected?",
    [28] = "Timed out. Wi-Fi may be slow or off.",
    [35] = "Secure connection failed. Check the device date and time.",
    [51] = "Secure connection failed. Check the device date and time.",
    [58] = "Secure connection failed. Check the device date and time.",
    [60] = "Certificate check failed. Check the device date and time.",
    [18] = "Download was cut short. Try again.",
    [56] = "Connection dropped. Try again.",
    [127] = "curl is not installed on this device.",
  }
  if m[r.rc] then return m[r.rc] end
  return "Network error (curl " .. tostring(r.rc) .. ")."
end

return net
