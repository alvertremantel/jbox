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
  style for a Breeze-flavored appearance; falls back gracefully on
  systems without the `qqc2-desktop-style` package installed.
- **Translucent, frameless** — sits on your Plasma desktop without
  feeling like a dialog.
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

```sh
# from this directory
uv venv -p 3.12 .venv
. .venv/bin/activate
uv pip install -e .

# run
jbox
```

Or, if you'd rather not install the project, just install PySide6 and
invoke the script directly:

```sh
uv run --with PySide6 python main.py
```

> The PySide6 wheels currently cap at Python 3.13, so pin the venv to
> 3.12 as shown above.

## Bind a key

In KDE Plasma: *System Settings → Shortcuts → Custom Shortcuts → Edit →
New → Global Shortcut → Command/URL*. Set:

- **Trigger**: e.g. `Super+R`, `F12`, `Alt+Space` — whatever you like.
- **Action**: `jbox` (or the absolute path to the venv's `jbox` binary).

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

`backend.AliasesModel.upsert(name, command, description)` is exposed
on the QML side, so adding a small "manage aliases" pane is a v2
feature (see *Future* below).

## History

`~/.config/jbox/history.json` — a list of `{command, count, last_used}`
records, most-recent first, capped at 200 entries. Edit or delete it
by hand; jbox will pick the changes up on next launch.

## File layout

```
.
├── main.py            # entry point, single-instance IPC, app setup
├── backend.py         # QObject + models for history, aliases, exec
├── qml/Main.qml       # the UI (translucent, frameless, bottom-left)
├── pyproject.toml     # project metadata + dep
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
