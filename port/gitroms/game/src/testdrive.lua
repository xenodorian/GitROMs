-- testdrive.lua: scripted run for PC testing only (GITROMS_TEST env var).
-- Steps separated by ";": idle, wait=<sec>, act=<action>, shot=<png path>.
local M = {}

function M.new(app)
  local steps = {}
  for s in (os.getenv("GITROMS_TEST") .. ";"):gmatch("([^;]*);") do
    if s ~= "" then steps[#steps + 1] = s end
  end
  local i, wait, pending_shot = 1, 0, nil
  local d = {}

  function d.update(dt)
    if pending_shot then return end
    if wait > 0 then wait = wait - dt; return end
    local st = steps[i]
    if not st then love.event.quit(); return end
    local S = app.state()
    if st == "idle" then
      if not S.task then i = i + 1; wait = 0.2 end
      return
    end
    local k, v = st:match("^(%w+)=(.*)$")
    i = i + 1
    if k == "wait" then wait = tonumber(v)
    elseif k == "act" then app.action(v); wait = 0.1
    elseif k == "shot" then pending_shot = v end
  end

  function d.afterDraw(canvas)
    if not pending_shot then return end
    local data = canvas:newImageData()
    local fd = data:encode("png")
    local f = io.open(pending_shot, "wb")
    if f then f:write(fd:getString()); f:close() end
    pending_shot = nil
  end
  return d
end

return M
