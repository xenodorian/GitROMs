-- main.lua: GitROMs entry point (LOVE 11.x). Draws to a fixed 640x480 canvas
-- and scales it to whatever the display reports.
local app = require("src.app")

local canvas
local W, H = 640, 480
local driver

function love.load()
  love.keyboard.setKeyRepeat(true)
  love.mouse.setVisible(false)
  love.graphics.setDefaultFilter("linear", "linear")
  canvas = love.graphics.newCanvas(W, H)
  app.load()
  if os.getenv("GITROMS_TEST") then driver = require("src.testdrive").new(app) end
end

function love.update(dt)
  if dt > 0.1 then dt = 0.1 end
  app.update(dt)
  if driver then driver.update(dt) end
end

function love.draw()
  love.graphics.setCanvas(canvas)
  love.graphics.clear(0, 0, 0, 1)
  app.draw()
  love.graphics.setCanvas()
  local ww, wh = love.graphics.getDimensions()
  local s = math.min(ww / W, wh / H)
  love.graphics.setColor(1, 1, 1, 1)
  love.graphics.draw(canvas, (ww - W * s) / 2, (wh - H * s) / 2, 0, s, s)
  if driver then driver.afterDraw(canvas) end
end

function love.keypressed(key, _, isrepeat) app.key(key, isrepeat) end
function love.gamepadpressed(_, button) app.pad(button) end
function love.quit() app.shutdown() end

function love.errorhandler(msg)
  pcall(app.log, "FATAL: " .. tostring(msg) .. "\n" .. debug.traceback())
  io.stderr:write(tostring(msg), "\n")
  return 1 -- exit with failure instead of looping on the blue screen
end
