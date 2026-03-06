# 3ds-epub

An EPUB reader for the Nintendo 3DS, built with devkitPro and citro2d.

## Features

- Read EPUB books on the bottom screen with word-wrapped, paginated text
- Horizontal (320x240) and vertical (240x320) reading orientations
- Adjustable font size with visual feedback
- Chapter title header on each page
- Page turn edge flash feedback
- Battery level indicator
- Library browser with saved reading progress per book
- Built-in HTTP server for uploading EPUBs wirelessly from any browser
- Automatic reading progress save/restore (chapter + page)
- Power-saving dirty flag rendering (skips GPU draws when idle)
- Loading screen feedback when opening books

## Building

Requires [devkitPro](https://devkitpro.org/) with the 3DS toolchain.

Install dependencies:
```bash
# In devkitPro MSYS2 shell
pacman -S 3ds-dev 3ds-zlib
```

Build:
```bash
# Windows: use the build script (auto-detects devkitPro MSYS2 shell)
./build.sh

# Or directly in devkitPro shell
make
```

Output: `3ds-epub.3dsx`

## Installation

1. Copy `3ds-epub.3dsx` to your 3DS SD card under `/3ds/3ds-epub/`
2. Launch via the Homebrew Launcher

The app creates this folder structure on the SD card:
```
sdmc:/3ds/3ds-epub/
  books/          - Downloaded .epub files
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
| X | Enter transfer mode |
| Touch | Tap to select, double-tap to open |
| START | Exit app |

### Reader (Horizontal)
| Input | Action |
|-------|--------|
| L/R or D-Pad Left/Right | Turn page |
| D-Pad Up/Down | Adjust font size |
| Y | Toggle orientation |
| B | Back to library |
| Touch | Tap right half = next, left half = prev |

### Reader (Vertical)
In vertical mode, d-pad controls are rotated to match the physical orientation:
| Input | Action |
|-------|--------|
| L/R or D-Pad Up/Down | Turn page |
| D-Pad Left/Right | Adjust font size |
| Y | Toggle orientation |
| B | Back to library |

## Project Structure

```
source/
  main.c          - Entry point, main loop
  app.h/c         - App state machine, screen transitions
  ui_library.h/c  - Library screen (book list, selection)
  ui_reader.h/c   - Reader screen (text display, pagination)
  epub.h/c        - EPUB parsing (ZIP -> OPF -> XHTML -> text)
  httpd.h/c       - HTTP server for file transfer
  config.h/c      - Reading progress persistence (JSON)
  util.h/c        - File/path utilities
lib/
  minizip/        - ZIP extraction (vendored from zlib)
  cJSON/          - JSON parsing (vendored)
romfs/
  upload.html     - Web upload form (embedded in .3dsx)
```

## License

MIT
