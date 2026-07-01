# jbox

A minimal, KDE-styled command launcher for Linux. A `Win+R` for the
terminal: a small translucent window pinned to the bottom-left of your
primary screen, with a single text field. Type a command or an alias,
press Enter, it runs in your shell (so your existing `~/.bashrc` aliases
just work). Press Escape, click outside, or hit the keybind again to
hide.

```
+--------------------------------------------------+
| ❯ vim                                              |
+--------------------------------------------------+
```

## Features

- **KDE look and feel** — uses the `org.kde.desktop` QtQuick.Controls
  style for a Breeze-flavored appearance (requires the
  `qqc2-desktop-style` package; pass `--style Fusion` or set
  `QT_QUICK_CONTROLS_STYLE=Fusion` if it isn't installed).
- **Translucent, frameless** — sits on your Plasma desktop without
  feeling like a dialog. Panel colors track your active KDE color scheme
  (`SystemPalette`), and KWin blur-behind is enabled so it reads as neutral
  Breeze grey over any wallpaper, the same as KRunner and the panels.
- **Bottom-left by default** — pinned to the primary screen, 16px
  margin. Repositions on resolution or screen changes.
- **Runs through your shell** — commands are executed with
  `bash -lc "..."`, so your existing `~/.bashrc` aliases and shell
  functions are available without any extra wiring.
- **Own alias database** — store jbox-specific shortcuts in
  `~/.config/jbox/aliases.json` so you can keep `.bashrc` clean.
- **History and suggestions** — most-recent commands and aliases show
  up as you type. Up/Down to navigate, Tab to autocomplete.
- **Single-instance toggle** — a second `jbox` invocation (your
  keybind) just toggles the existing window. No extra process spawned.
- **XDG config** — all data lives in `~/.config/jbox/`.

## Install

Build dependencies (Fedora):

```sh
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel cmake ninja-build gcc-c++ kf6-qqc2-desktop-style kf6-kwindowsystem-devel
```

Debian/Ubuntu equivalents: `qt6-base-dev qt6-declarative-dev
qml6-module-qtquick-controls2 kf6-kwindowsystem-dev` (Debian package name may
vary; look for `libkf6windowsystem-dev`). Arch: `qt6-base qt6-declarative
kwindowsystem`.

```sh
# from this directory
cmake --preset default
cmake --build --preset default

# run
./build/jbox
```

To install for your user — binary to `~/.local/bin`, plus a `.desktop`
entry so jbox shows up in KRunner/the app menu:

```sh
./install.sh
```

Safe to re-run any time you rebuild; it clears out its own previous install
first, so there's no manual cleanup between iterations.

## Bind a key

In KDE Plasma: *System Settings → Shortcuts → Custom Shortcuts → Edit →
New → Global Shortcut → Command/URL*. Set:

- **Trigger**: e.g. `Super+R`, `F12`, `Alt+Space` — whatever you like.
- **Action**: `jbox` (if installed to your `$PATH`) or the absolute
  path to the built binary, e.g. `~/dev/gen/tools/jbox/build/jbox`.

Plasma will then start jbox the first time, and every subsequent press
toggles the existing instance.

For GNOME, use *Settings → Keyboard → Custom Shortcuts*; for Sway/i3,
add a `bindsym` running `jbox &`.

## Aliases

Aliases live in `~/.config/jbox/aliases.json` as a JSON array:

```json
[
  {
    "name": "d",
    "command": "kitty --directory \"$HOME/projects/dev\"",
    "description": "Open a kitty at the dev projects dir"
  },
  {
    "name": "todo",
    "command": "nvim ~/notes/todo.md",
    "description": "Edit today's TODO file"
  }
]
```

`backend.aliases.upsert(name, command, description)` is exposed on
the QML side, so adding a small "manage aliases" pane is a v2 feature
(see *Future* below).

## History

`~/.config/jbox/history.json` — a list of `{command, count, last_used}`
records, most-recent first, capped at 200 entries. Edit or delete it
by hand; jbox will pick the changes up on next launch.

## File layout

```
.
├── CMakeLists.txt         # build definition
├── CMakePresets.json      # `default`/`release` configure+build presets
├── install.sh             # build + user-local install (~/.local/bin + .desktop entry)
├── src/
│   ├── main.cpp           # entry point, CLI parsing, app setup
│   ├── launcherbackend.*  # QObject exposed to QML as `backend`
│   ├── historymodel.*     # history list model + persistence
│   ├── aliasesmodel.*     # aliases list model + persistence
│   ├── jsonstore.*        # shared atomic JSON read/write helpers
│   ├── terminal.*         # terminal emulator detection + launch
│   ├── ipcserver.*        # single-instance IPC (QLocalServer/Socket)
│   └── windowcontroller.* # show/hide/toggle for the QML root window
├── qml/Main.qml           # the UI (translucent, frameless, bottom-left)
├── README.md
└── .gitignore
```

## Tweaking

The hard-coded panel color, fonts, and 16px screen margin live near the
top of `qml/Main.qml`. To re-anchor to a different corner, edit the
`_reposition` function.

To switch from `org.kde.desktop` to a different QtQuick.Controls style
(Fusion, Material, …) pass `--style Fusion` or set
`QT_QUICK_CONTROLS_STYLE=Fusion`.

## Future (likely v2)

- **Inline alias manager** — a small modal to add/edit/remove aliases
  from the launcher itself, calling `backend.aliases.upsert()`.
- **Recent-only / pinned items** — a fast `Ctrl+1..9` to re-run the Nth
  most recent command.
- **Background command runner** — run a command without stealing focus
  from the current app (a "silent" mode).
- **Kirigami upgrade** — drop the `org.kde.desktop` style and use
  Kirigami 2 (KF6) for an even more native feel, including responsive
  layouts if jbox is ever scaled up.
- **Fuzzy matching** — `fzf`-style scoring instead of plain `startswith`.

## License

Pick whatever suits you; this is a personal-utility skeleton.
