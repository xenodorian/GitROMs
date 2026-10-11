-- conf.lua: GitROMs, 640x480 (R36S native).
function love.conf(t)
  t.identity = "gitroms"
  t.version = "11.5"
  t.console = false

  t.window.title = "GitROMs"
  t.window.width = 640
  t.window.height = 480
  t.window.resizable = false
  t.window.fullscreen = (os.getenv("GITROMS_WINDOWED") == nil)
  t.window.vsync = 1

  t.modules.audio = false
  t.modules.sound = false
  t.modules.physics = false
  t.modules.video = false
  t.modules.touch = false
  t.modules.thread = false
end
