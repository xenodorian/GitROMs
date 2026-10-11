# GitROMs configuration

# UI colors (dark theme + signal orange)
BG = (16, 18, 16)
SURFACE = (24, 28, 24)
RAISED = (35, 41, 36)
FG = (231, 239, 228)
MUTED = (168, 181, 166)
LINE = (49, 56, 50)
SIGNAL = (230, 162, 60)
INK = (22, 19, 12)

# Screen (R36S is 640x480)
WIDTH = 640
HEIGHT = 480
FPS = 30

# Default GitHub user
DEFAULT_USER = "xenodorian"

# Only these extensions are ever shown or downloaded
ROM_EXTENSIONS = {
    ".nes", ".smc", ".sfc", ".gba", ".gbc", ".gb",
    ".md", ".gen", ".bin", ".sms", ".gg", ".pce",
    ".z64", ".n64", ".v64", ".nds",
    ".chd", ".pbp", ".iso", ".cso", ".cue",
    ".zip", ".7z",
}

# Map extension -> ArkOS rom folder name
EXT_TO_SYSTEM = {
    ".nes": "nes",
    ".smc": "snes",
    ".sfc": "snes",
    ".gba": "gba",
    ".gbc": "gbc",
    ".gb": "gb",
    ".md": "megadrive",
    ".gen": "megadrive",
    ".bin": "megadrive",
    ".sms": "mastersystem",
    ".gg": "gamegear",
    ".pce": "pcengine",
    ".z64": "n64",
    ".n64": "n64",
    ".v64": "n64",
    ".nds": "nds",
    ".chd": "psx",
    ".pbp": "psx",
    ".iso": "psp",
    ".cso": "psp",
    ".cue": "psx",
    ".zip": "arcade",
    ".7z": "arcade",
}

# Possible locations of the roms partition on ArkOS / R36S
ROM_ROOT_CANDIDATES = [
    "/roms",
    "/roms2",
    "/storage/roms",
    "/mnt/SDCARD/roms",
    "/mnt/sdcard/roms",
    "/media/roms",
    "/EASYROMS",
]

# GitHub API
GITHUB_API = "https://api.github.com"
USER_AGENT = "GitROMs-R36S/1.1"
GITHUB_TOKEN = None
