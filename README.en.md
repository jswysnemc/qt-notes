# qt-notes

A Linux desktop sticky notes application built with Qt 6 Widgets, designed for borderless detached-window workflows on Wayland / niri.

[中文](README.md)

## Features

### Window & Interaction

- Custom frameless windows; title bar dragging via `startSystemMove()`, Wayland-compatible
- Multiple notes open as independent windows, each maintaining its own window state
- Configurable startup behavior: open last closed, last edited, or last created note
- Title bar buttons: note list, theme toggle, settings, new note, close
- Double-click the title bar to rename a note

### Note List

- Multi-select, select all, and batch delete
- Double-click an entry to rename its title
- Left-click switches the note in the current window; right-click opens it in a separate window
- Sorted by last edit time by default; configurable to sort by creation time or title

### Encryption

- Per-note encryption: title and content stored as ciphertext in SQLite
- Two-tier global password scheme: simple password + recovery password
- After 3 consecutive failed simple-password attempts, the recovery password is required
- Encrypted note titles are partially masked in lists and when locked (first half visible, rest replaced with `*`) for easy identification
- Notes with image attachments cannot be encrypted (prevents plaintext asset exposure on disk)
- Encryption scheme: XChaCha20-Poly1305 + Argon2id key derivation; data key can be stored in the system keyring (libsecret)

### Editor

- System font picker with recently-used font list
- Font and font size are global settings; changes apply to all notes
- Toggle word wrap on/off
- `Ctrl + Scroll` to adjust font size
- Paste images from clipboard or drag in local image files
- Large images are automatically scaled with storage size limits; right-click for preview, copy, or delete
- Click an image or use the context menu to open a preview dialog showing the original size with scroll support
- Horizontal and vertical scrolling

### Storage

- Auto-save for text, theme, font, word-wrap settings, and window geometry
- Delete the current note from settings with confirmation prompt

## Build

Dependencies: Qt 6 (Core, Gui, Widgets, Sql), libsodium, libsecret

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/qt-notes
```

Install dependencies on Arch Linux:

```bash
pacman -S qt6-base libsodium libsecret
```

## Data Locations

| Data | Path |
|------|------|
| Database | `~/.local/share/snemc/qt-notes/notes.db` |
| Image assets | `~/.local/share/snemc/qt-notes/assets/` |
| App settings | `~/.config/snemc/qt-notes.ini` |
| Unlock state | `~/.local/share/snemc/qt-notes/unlock/` |

## Wayland / niri Notes

- `qt-notes` uses custom frameless windows with `startSystemMove()` for title bar dragging, compatible with Wayland
- To have notes open as floating windows by default, configure window rules for `qt-notes` in `niri`; install `qt-notes.desktop` for stable `app-id` matching
- Window size restoration is reliable
- Window position behavior varies by platform:
  - **X11**: The application saves and restores window coordinates and dimensions
  - **Wayland / niri**: The application saves dimensions and the last screen; exact positioning is controlled by the compositor

## License

MIT
