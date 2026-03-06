# 3ds-epub

An EPUB reader for the Nintendo 3DS, built with devkitPro and citro2d.

## Features

- Read EPUB books on the bottom screen with word-wrapped, paginated text
- Horizontal (320×240) and vertical (240×320) reading orientations
- Dark mode for comfortable night reading
- Adjustable font size with visual overlay feedback
- Page turn edge flash feedback
- Battery level indicator (color-coded: green charging, red low)
- Library browser with saved reading progress, sorting (A-Z, Z-A, last read), and book deletion
- Built-in HTTP server for uploading EPUBs wirelessly from any browser
- Full progress persistence: chapter, page, font size, orientation, and dark mode
- Auto-save on chapter change, every 10 page turns, HOME button, lid close, and exit
- Power-saving dirty flag rendering (skips GPU draws when idle)
- SELECT to toggle top screen backlight off (saves battery while reading)
- Installable as .cia (shows in HOME menu) or .3dsx (Homebrew Launcher)

## Building

Requires [devkitPro](https://devkitpro.org/) with the 3DS toolchain.

Install dependencies:
```bash
# In devkitPro MSYS2 shell
pacman -S 3ds-dev 3ds-zlib
```

Build:
```bash
make          # builds 3ds-epub.3dsx
make cia      # builds 3ds-epub.cia (installable via FBI)
```

## Installation

### .3dsx (Homebrew Launcher)
1. Copy `3ds-epub.3dsx` to `/3ds/3ds-epub/` on your SD card
2. Launch via the Homebrew Launcher

### .cia (HOME Menu)
1. Copy `3ds-epub.cia` to your SD card
2. Install with FBI or another CIA installer
3. Launch from the HOME menu

The app creates this folder structure on the SD card:
```
sdmc:/3ds/3ds-epub/
  books/          - Your .epub files
  cache/          - Extracted EPUB content (auto-managed)
  progress.json   - Reading positions per book
```

## Transferring Books

1. In the library screen, press **X** to enter Transfer Mode
2. The 3DS starts an HTTP server and shows its IP address
3. Open `http://<3DS_IP>:8080` in any browser on the same network
4. Use the upload form to send `.epub` files (max 50MB)
5. Press **B** to return to the library

## Controls

### Library
| Input | Action |
|-------|--------|
| D-Pad Up/Down | Navigate book list |
| A | Open selected book |
| Y | Delete book (press twice to confirm) |
| L / R | Cycle sort mode (A-Z, Z-A, Recent) |
| X | Enter transfer mode |
| Touch | Tap to select, tap again to open |
| SELECT | Toggle top screen backlight |
| START | Exit app |

### Reader
| Input | Horizontal | Vertical |
|-------|-----------|----------|
| L/R | Turn page | Turn page |
| D-Pad Left/Right | Turn page | Adjust font size |
| D-Pad Up/Down | Adjust font size | Turn page |
| X + D-Pad | Jump chapter | Jump chapter |
| A | Toggle dark mode | Toggle dark mode |
| Y | Toggle orientation | Toggle orientation |
| B | Back to library | Back to library |
| Touch | Tap right/left half to turn page | Tap bottom/top half to turn page |
| SELECT | Toggle top screen backlight | Toggle top screen backlight |

## Project Structure

```
source/
  main.c          - Entry point, main loop
  app.h/c         - App state machine, screen transitions
  ui_library.h/c  - Library screen (book list, sorting, deletion)
  ui_reader.h/c   - Reader screen (text display, pagination, dark mode)
  epub.h/c        - EPUB parsing (ZIP → OPF → XHTML → text)
  httpd.h/c       - HTTP server for file transfer
  config.h/c      - Reading progress persistence (JSON)
  util.h/c        - File/path utilities
lib/
  minizip/        - ZIP extraction (vendored from zlib)
  cJSON/          - JSON parsing (vendored)
meta/
  app.rsf         - ROM spec file for CIA builds
assets/
  icon.png        - App icon (48×48, used for SMDH and CIA)
romfs/
  upload.html     - Web upload form (embedded in binary)
```

## License

MIT
