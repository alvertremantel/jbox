"""Backend logic for jbox: command execution, history, and aliases.

Exposed to QML as the `backend` context property (see main.py).
"""

from __future__ import annotations

import json
import os
import shlex
import shutil
import subprocess
from pathlib import Path
from typing import Any

from PySide6.QtCore import (
    QAbstractListModel,
    QModelIndex,
    QObject,
    Qt,
    Signal,
    Slot,
    Property,
)


# ---------------------------------------------------------------------------
# Persistence helpers
# ---------------------------------------------------------------------------


def _atomic_write(path: Path, payload: str) -> None:
    """Write to a temp file in the same directory and rename.

    Avoids leaving a half-written file if the process is killed mid-write.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(payload, encoding="utf-8")
    tmp.replace(path)


def _read_json(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        # Corrupt file: rename it aside and start fresh so we never lose data silently.
        try:
            path.rename(path.with_suffix(path.suffix + ".corrupt"))
        except OSError:
            pass
        return default


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------


class HistoryModel(QAbstractListModel):
    """A simple list of `{command, count, last_used}` rows, most-recent first."""

    CommandRole = Qt.UserRole + 1
    CountRole = Qt.UserRole + 2

    def __init__(self, history_file: Path, max_size: int = 200) -> None:
        super().__init__()
        self._file = history_file
        self._max_size = max_size
        self._items: list[dict[str, Any]] = _read_json(history_file, [])

    def _save(self) -> None:
        _atomic_write(self._file, json.dumps(self._items, indent=2))

    def add(self, command: str) -> None:
        # Bump the count if the command already exists, otherwise prepend it.
        now = _now()
        for i, item in enumerate(self._items):
            if item["command"] == command:
                item["count"] = int(item.get("count", 0)) + 1
                item["last_used"] = now
                # Move to the front to reflect "most recent".
                self._items.insert(0, self._items.pop(i))
                self._save()
                self.layoutChanged.emit()
                return
        self._items.insert(0, {"command": command, "count": 1, "last_used": now})
        if len(self._items) > self._max_size:
            self._items = self._items[: self._max_size]
        self._save()
        self.layoutChanged.emit()

    def search(self, prefix: str, limit: int = 8) -> list[dict[str, Any]]:
        """Return up to `limit` history items that start with `prefix`."""
        prefix = prefix.strip()
        if not prefix:
            return []
        return [item for item in self._items if item["command"].startswith(prefix)][
            :limit
        ]

    # --- QAbstractListModel API ---------------------------------------------

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:  # noqa: B008
        return 0 if parent.isValid() else len(self._items)

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole):
        if not index.isValid() or not (0 <= index.row() < len(self._items)):
            return None
        item = self._items[index.row()]
        if role in (self.CommandRole, Qt.DisplayRole):
            return item["command"]
        if role == self.CountRole:
            return int(item.get("count", 0))
        return None

    def roleNames(self) -> dict[int, bytes]:  # type: ignore[override]
        return {
            self.CommandRole: b"command",
            self.CountRole: b"count",
        }

    @Slot(str)
    def remove(self, command: str) -> None:
        self._items = [i for i in self._items if i["command"] != command]
        self._save()
        self.layoutChanged.emit()


class AliasesModel(QAbstractListModel):
    """A list of `{name, command, description}` rows."""

    NameRole = Qt.UserRole + 1
    CommandRole = Qt.UserRole + 2
    DescriptionRole = Qt.UserRole + 3

    def __init__(self, aliases_file: Path) -> None:
        super().__init__()
        self._file = aliases_file
        self._items: list[dict[str, str]] = _read_json(aliases_file, [])

    def _save(self) -> None:
        _atomic_write(self._file, json.dumps(self._items, indent=2))

    def resolve(self, query: str) -> str | None:
        """Return the command bound to a given name, or None."""
        for item in self._items:
            if item.get("name") == query:
                return item.get("command", "")
        return None

    def search(self, prefix: str, limit: int = 8) -> list[dict[str, str]]:
        prefix = prefix.strip().lower()
        if not prefix:
            return []
        return [
            item
            for item in self._items
            if item.get("name", "").lower().startswith(prefix)
        ][:limit]

    # --- QAbstractListModel API ---------------------------------------------

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:  # noqa: B008
        return 0 if parent.isValid() else len(self._items)

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole):
        if not index.isValid() or not (0 <= index.row() < len(self._items)):
            return None
        item = self._items[index.row()]
        if role == self.NameRole:
            return item.get("name", "")
        if role == self.CommandRole:
            return item.get("command", "")
        if role == self.DescriptionRole:
            return item.get("description", "")
        if role == Qt.DisplayRole:
            return item.get("name", "")
        return None

    def roleNames(self) -> dict[int, bytes]:  # type: ignore[override]
        return {
            self.NameRole: b"name",
            self.CommandRole: b"command",
            self.DescriptionRole: b"description",
        }

    @Slot(str, str, str)
    def upsert(self, name: str, command: str, description: str = "") -> None:
        name = name.strip()
        if not name:
            return
        for item in self._items:
            if item.get("name") == name:
                item["command"] = command
                item["description"] = description
                self._save()
                self.layoutChanged.emit()
                return
        self._items.append(
            {"name": name, "command": command, "description": description}
        )
        self._save()
        self.layoutChanged.emit()

    @Slot(str)
    def remove(self, name: str) -> None:
        self._items = [i for i in self._items if i.get("name") != name]
        self._save()
        self.layoutChanged.emit()


# ---------------------------------------------------------------------------
# Terminal emulator detection
# ---------------------------------------------------------------------------

# Each tuple is (binary_name, [arg_list_before_command]).
# The command itself (e.g. "bash -lc '...'") is appended after these
# arguments. We try them in order and cache the first that exists on PATH.
_TERMINAL_CANDIDATES: list[tuple[str, list[str]]] = [
    ("konsole", ["-e"]),  # KDE:  konsole -e bash -lc "..."
    (
        "gnome-terminal",
        ["--wait", "--"],
    ),  # GNOME: gnome-terminal --wait -- bash -lc "..."
    ("xfce4-terminal", ["-e"]),  # XFCE:  xfce4-terminal -e bash -lc "..."
    ("alacritty", ["-e"]),  # alacritty -e bash -lc "..."
    ("kitty", ["--"]),  # kitty -- bash -lc "..."
    ("wezterm", ["start", "--"]),  # wezterm start -- bash -lc "..."
    ("foot", ["--"]),  # foot -- bash -lc "..."
    ("tilix", ["-e"]),  # tilix -e bash -lc "..."
    ("terminator", ["-e"]),  # terminator -e bash -lc "..."
    ("xterm", ["-e"]),  # xterm -e bash -lc "..."
    ("uxterm", ["-e"]),
    ("rxvt", ["-e"]),
    ("urxvt", ["-e"]),
]


def _find_terminal() -> tuple[str, list[str]] | None:
    """Return the first available terminal emulator and its lead-in args, or None."""
    # Try Debian/Ubuntu's preferred alternative first.
    x_term = shutil.which("x-terminal-emulator")
    if x_term:
        # x-terminal-emulator is almost always a symlink to the actual
        # terminal; for arguments, assume the common "-e" convention.
        return (x_term, ["-e"])

    for name, args in _TERMINAL_CANDIDATES:
        if shutil.which(name):
            return (name, args)
    return None


# Cache at import time so we don't walk PATH on every command.
_TERMINAL: tuple[str, list[str]] | None = _find_terminal()


def _launch_terminal(command: str, shell: str) -> int:
    """Launch `command` inside a terminal emulator window.

    The command is run inside `bash -lc` so the user's login shell aliases
    are available. Returns -1 if no terminal emulator is available, 0 on
    successful launch (without waiting for the command to finish).
    """
    if _TERMINAL is None:
        return -1
    term, lead = _TERMINAL
    argv = [term, *lead, shell, "-lc", command]
    try:
        subprocess.Popen(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        return 0
    except FileNotFoundError:
        return -1


# ---------------------------------------------------------------------------
# Backend
# ---------------------------------------------------------------------------


def _now() -> int:
    """Wall-clock seconds since the epoch. Centralized so tests can monkey-patch it."""
    import time

    return int(time.time())


class LauncherBackend(QObject):
    """The single QObject exposed to QML.

    The QML side calls `run(query)` (launches in a terminal emulator) or
    `runCapture(query)` (captures output inline), listens to `statusChanged`
    for transient feedback, and can browse the `history` and `aliases` models.
    """

    statusChanged = Signal(str)  # short status string ("running: …", "exit 0: …")
    outputReceived = Signal(str)  # captured stdout/stderr of `runCapture`
    commandStarted = Signal(str)
    commandFinished = Signal(int)  # exit code, or -1 on error

    def __init__(self, aliases_file: Path, history_file: Path) -> None:
        super().__init__()
        self._aliases = AliasesModel(aliases_file)
        self._history = HistoryModel(history_file)
        self._shell: str = os.environ.get("SHELL", "/bin/bash")

    # --- Properties exposed to QML -----------------------------------------

    @Property(QObject, constant=True)
    def history(self) -> HistoryModel:  # type: ignore[override]
        return self._history

    @Property(QObject, constant=True)
    def aliases(self) -> AliasesModel:  # type: ignore[override]
        return self._aliases

    @Property(str, constant=True)
    def shell(self) -> str:  # type: ignore[override]
        return self._shell

    _has_terminal_cache: bool | None = None

    @Property(bool, constant=True)
    def hasTerminal(self) -> bool:  # type: ignore[override]
        """Whether a supported terminal emulator was found at startup."""
        if self._has_terminal_cache is None:
            self._has_terminal_cache = _TERMINAL is not None
        return self._has_terminal_cache

    # --- Slots called from QML ---------------------------------------------

    @Slot(str, result=str)
    def expand(self, query: str) -> str:
        """If `query` matches an alias name, return the alias's command; else echo."""
        resolved = self._aliases.resolve(query.strip())
        return resolved if resolved is not None else query

    @Slot(str)
    def run(self, command: str) -> None:
        """Launch `command` in a terminal emulator window.

        The terminal pops up immediately; jbox hides itself and doesn't
        wait for the command to complete. Falls back to fire-and-forget
        background execution if no terminal is available.
        """
        command = command.strip()
        if not command:
            return
        resolved = self._aliases.resolve(command) or command
        rc = _launch_terminal(resolved, self._shell)
        if rc < 0:
            # No terminal available — fall back to silent background launch
            # so the command at least runs.
            self._run_background(resolved)
        else:
            self.commandStarted.emit(command)
            self.commandFinished.emit(-1)
        self._history.add(command)

    @Slot(str)
    def runCapture(self, command: str) -> None:
        """Run `command` synchronously with a timeout, emit captured output."""
        command = command.strip()
        if not command:
            return
        resolved = self._aliases.resolve(command) or command
        rc = self._run_capture(resolved, timeout=10.0)
        self._history.add(command)
        if rc == 0:
            self.statusChanged.emit(f"ok: {resolved}")
        else:
            self.statusChanged.emit(f"exit {rc}: {resolved}")

    @Slot(str, result="QVariantList")
    def suggestHistory(self, prefix: str) -> list:
        return [
            {"command": item["command"], "count": int(item.get("count", 0))}
            for item in self._history.search(prefix)
        ]

    @Slot(str, result="QVariantList")
    def suggestAliases(self, prefix: str) -> list:
        return [
            {
                "name": item["name"],
                "command": item.get("command", ""),
                "description": item.get("description", ""),
            }
            for item in self._aliases.search(prefix)
        ]

    # --- Internal ----------------------------------------------------------

    def _run_background(self, command: str) -> int:
        """Fire-and-forget: detach the process, discard output."""
        argv = [self._shell, "-lc", command]
        self.commandStarted.emit(command)
        self.statusChanged.emit(f"running: {command}")
        try:
            subprocess.Popen(
                argv,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            self.commandFinished.emit(-1)
            return 0
        except FileNotFoundError as exc:
            self.outputReceived.emit(f"error: {exc}")
            self.statusChanged.emit(f"error: {exc}")
            self.commandFinished.emit(-1)
            return -1

    def _run_capture(self, command: str, *, timeout: float | None = None) -> int:
        """Execute `command` in a login shell and capture stdout/stderr.

        Returns the exit code, or -1 on launch failure/timout.
        """
        argv = [self._shell, "-lc", command]
        self.commandStarted.emit(command)
        self.statusChanged.emit(f"running: {command}")
        try:
            result = subprocess.run(
                argv,
                capture_output=True,
                text=True,
                timeout=timeout,
                start_new_session=True,
            )
            tail = (result.stdout + result.stderr).strip().splitlines()
            self.outputReceived.emit("\n".join(tail[-32:]) if tail else "(no output)")
            self.commandFinished.emit(result.returncode)
            return result.returncode
        except subprocess.TimeoutExpired:
            self.outputReceived.emit(f"error: timed out after {timeout}s")
            self.commandFinished.emit(-1)
            return -1
        except FileNotFoundError as exc:
            self.outputReceived.emit(f"error: {exc}")
            self.statusChanged.emit(f"error: {exc}")
            self.commandFinished.emit(-1)
            return -1
        except Exception as exc:  # noqa: BLE001 - we genuinely want to catch all
            self.outputReceived.emit(f"error: {exc}")
            self.statusChanged.emit(f"error: {exc}")
            self.commandFinished.emit(-1)
            return -1
