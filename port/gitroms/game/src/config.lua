-- config.lua: edit this file on a PC to change user, file types or folders.
local cfg = {}

-- GitHub user whose public repositories are listed.
cfg.user = "xenodorian"

-- Only these extensions are shown or downloaded (matched case-insensitively).
local exts = {
  ".nes", ".smc", ".sfc", ".gba", ".sgba", ".gbc", ".gb",
  ".md", ".gen", ".bin", ".sms", ".gg", ".pce",
  ".z64", ".n64", ".v64", ".nds",
  ".chd", ".pbp", ".iso", ".cso", ".cue",
  ".pak", ".zip", ".7z",
}
cfg.extensions = {}
for _, e in ipairs(exts) do cfg.extensions[e] = true end

-- Default ArkOS rom folder for each extension. The user can change it with
-- left/right on the download screen before saving.
cfg.ext_to_system = {
  [".nes"] = "nes", [".smc"] = "snes", [".sfc"] = "snes",
  [".gba"] = "gba", [".sgba"] = "gba", [".gbc"] = "gbc", [".gb"] = "gb",
  [".md"] = "megadrive", [".gen"] = "megadrive", [".bin"] = "megadrive",
  [".sms"] = "mastersystem", [".gg"] = "gamegear", [".pce"] = "pcengine",
  [".z64"] = "n64", [".n64"] = "n64", [".v64"] = "n64", [".nds"] = "nds",
  [".chd"] = "psx", [".pbp"] = "psx", [".cue"] = "psx",
  [".iso"] = "psp", [".cso"] = "psp",
  [".pak"] = "gba", [".zip"] = "arcade", [".7z"] = "arcade",
}

-- Folders offered when changing the destination.
cfg.systems = {
  "gba", "gbc", "gb", "nes", "snes", "megadrive", "mastersystem", "gamegear",
  "pcengine", "n64", "nds", "psx", "psp", "arcade", "fbneo", "mame", "ports",
}

-- Places the roms partition may be mounted (first writable one wins).
-- The environment variable GITROMS_ROMS overrides this list.
cfg.rom_roots = {
  "/roms", "/roms2", "/storage/roms", "/mnt/SDCARD/roms",
  "/mnt/sdcard/roms", "/media/roms", "/EASYROMS",
}

cfg.api = os.getenv("GITROMS_API") or "https://api.github.com"
cfg.user_agent = "GitROMs-R36S/2.0"

return cfg
