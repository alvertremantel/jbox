"""jbox — a minimal KDE-styled command launcher.

Usage
-----
    jbox                 # show the window (or toggle if another instance exists)
    jbox --toggle        # same as `jbox`; this is what your keybind will run
    jbox --no-fork       # skip the single-instance dance (debugging)
    jbox --print-cfg     # print the resolved config paths and exit
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from PySide6.QtCore import (
    QCoreApplication,
    QObject,
    QUrl,
    Signal,
    Slot,
)
from PySide6.QtGui import QGuiApplication
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuickControls2 import QQuickStyle

from backend import LauncherBackend


SOCKET_NAME = "jbox.singleton"


# ---------------------------------------------------------------------------
# Config paths
# ---------------------------------------------------------------------------


def config_dir() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))
    return Path(base) / "jbox"


def aliases_path() -> Path:
    return config_dir() / "aliases.json"


def history_path() -> Path:
    return config_dir() / "history.json"


# ---------------------------------------------------------------------------
# Single-instance plumbing
# ---------------------------------------------------------------------------


def _send_toggle(
    socket_name: str, payload: bytes = b"toggle\n", timeout_ms: int = 200
) -> bool:
    """Try to deliver `payload` to an already-running jbox instance.

    Returns True if a primary instance is already running and the message
    was delivered; False otherwise (caller should become primary).
    """
    sock = QLocalSocket()
    sock.connectToServer(socket_name)
    if not sock.waitForConnected(timeout_ms):
        return False
    sock.write(payload)
    sock.flush()
    sock.waitForBytesWritten(timeout_ms)
    sock.disconnectFromServer()
    return True


def _style_works(style: str) -> bool:
    """Probe whether `style` is a loadable QtQuick.Controls style.

    Done in a fresh subprocess because a failed import is cached for the
    lifetime of the calling Python process.
    """
    import subprocess
    import tempfile

    probe = b"import QtQuick\nimport QtQuick.Controls\nItem { }\n"
    probe_path = Path(tempfile.gettempdir()) / "jbox-style-probe.qml"
    try:
        probe_path.write_bytes(probe)
        code = (
            "import os, sys\n"
            f"os.environ['QT_QUICK_CONTROLS_STYLE'] = {style!r}\n"
            "from PySide6.QtCore import QUrl\n"
            "from PySide6.QtGui import QGuiApplication\n"
            "from PySide6.QtQml import QQmlApplicationEngine\n"
            "app = QGuiApplication([])\n"
            f"url = QUrl.fromLocalFile({str(probe_path)!r})\n"
            "eng = QQmlApplicationEngine()\n"
            "eng.load(url)\n"
            "sys.exit(0 if eng.rootObjects() else 1)\n"
        )
        # Pass through QT_QPA_PLATFORM if the caller set it (e.g. offscreen tests).
        env = {k: v for k, v in os.environ.items() if k != "QT_QUICK_CONTROLS_STYLE"}
        result = subprocess.run(
            [sys.executable, "-c", code],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5,
        )
        return result.returncode == 0
    except (subprocess.TimeoutExpired, OSError):
        return False
    finally:
        try:
            probe_path.unlink()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# IPC server (lives in the primary instance)
# ---------------------------------------------------------------------------


class IpcServer(QObject):
    """Forwards 'toggle' messages from a second invocation to the QML window."""

    toggleRequested = Signal()

    def __init__(self, socket_name: str) -> None:
        super().__init__()
        self._server = QLocalServer(self)
        # Stale socket files from a previous crash are the classic QLocalServer
        # footgun — clear before listening.
        QLocalServer.removeServer(socket_name)
        if not self._server.listen(socket_name):
            print(
                f"jbox: failed to start IPC server: {self._server.errorString()}",
                file=sys.stderr,
            )
            return
        self._server.newConnection.connect(self._on_new_connection)

    @Slot()
    def _on_new_connection(self) -> None:
        sock = self._server.nextPendingConnection()
        if sock is None:
            return
        # Drain whatever the client sent; we only act on 'toggle' but we
        # want the client's waitForBytesWritten to complete promptly.
        sock.readyRead.connect(lambda s=sock: self._drain(s))
        sock.disconnected.connect(sock.deleteLater)

    def _drain(self, sock: QLocalSocket) -> None:
        data = bytes(sock.readAll()).decode("utf-8", errors="ignore").strip()
        if data == "toggle":
            self.toggleRequested.emit()
        elif data == "quit":
            QCoreApplication.quit()


# ---------------------------------------------------------------------------
# Window controller
# ---------------------------------------------------------------------------


class WindowController(QObject):
    """Owns the QML root object and the show/hide bookkeeping."""

    def __init__(self, engine: QQmlApplicationEngine) -> None:
        super().__init__()
        # Grab the first ApplicationWindow the engine produced.
        roots = engine.rootObjects()
        if not roots:
            raise RuntimeError("QML root object failed to load; see QML errors above.")
        self._win = roots[0]
        # Hide on startup; we only become visible when toggled.
        self._win.setProperty("visible", False)
        # We don't want the app to quit when the window is hidden — we want
        # the launcher to stay running in the background until explicitly quit.
        QGuiApplication.instance().setQuitOnLastWindowClosed(False)

    @Slot()
    def toggle(self) -> None:
        if self._win.property("visible"):
            self._win.setProperty("visible", False)
        else:
            # The QML side pulls focus into the TextField via its
            # onActiveFocusChanged handler; we just need to surface the window.
            self._win.setProperty("visible", True)

    @Slot()
    def show(self) -> None:
        self._win.setProperty("visible", True)

    @Slot()
    def hide(self) -> None:
        self._win.setProperty("visible", False)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def _parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="jbox",
        description="A minimal KDE-styled command launcher.",
    )
    p.add_argument(
        "--toggle",
        action="store_true",
        help="If a primary instance is running, toggle it; otherwise become primary and show.",
    )
    p.add_argument(
        "--no-single-instance",
        action="store_true",
        help="Skip the single-instance check (useful when debugging the QML).",
    )
    p.add_argument(
        "--print-cfg",
        action="store_true",
        help="Print resolved config paths and exit.",
    )
    p.add_argument(
        "--style",
        default=os.environ.get("QT_QUICK_CONTROLS_STYLE", "org.kde.desktop"),
        help="QtQuick.Controls style (default: org.kde.desktop).",
    )
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv if argv is not None else sys.argv[1:])

    if args.print_cfg:
        print(f"config dir: {config_dir()}")
        print(f"aliases:    {aliases_path()}")
        print(f"history:    {history_path()}")
        return 0

    # Make sure the config dir exists before we hand paths to the backend,
    # so the first ever launch doesn't surprise the user with no suggestions.
    config_dir().mkdir(parents=True, exist_ok=True)

    # Single-instance: if another jbox is already running, ping it and exit.
    if not args.no_single_instance:
        if _send_toggle(SOCKET_NAME):
            return 0

    # Application identity — used by QStandardPaths, settings, and the WM.
    QCoreApplication.setOrganizationName("jbox")
    QCoreApplication.setApplicationName("jbox")

    app = QGuiApplication(sys.argv)
    # The launcher survives window close; we only exit on explicit `quit` IPC
    # or a fatal error.
    app.setQuitOnLastWindowClosed(False)

    backend = LauncherBackend(aliases_path(), history_path())

    # The KDE-flavored QQC2 style is provided by the `qqc2-desktop-style`
    # package (separate from PySide6 base). We probe each candidate in a
    # short-lived child process, because once a QML import fails the
    # result is cached for the lifetime of the process and we can't
    # cleanly fall back from within the same Python interpreter.
    candidates: list[str] = []
    for s in [args.style, "Fusion", "Default"]:
        if s and s not in candidates:
            candidates.append(s)

    chosen = next((s for s in candidates if _style_works(s)), "Fusion")
    if chosen != args.style:
        print(
            f"jbox: style '{args.style}' not available; using '{chosen}'.",
            file=sys.stderr,
        )
    QQuickStyle.setStyle(chosen)

    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("backend", backend)
    qml_path = Path(__file__).resolve().parent / "qml" / "Main.qml"
    engine.load(QUrl.fromLocalFile(str(qml_path)))
    if not engine.rootObjects():
        print(f"jbox: failed to load QML ({qml_path}).", file=sys.stderr)
        return 1

    controller = WindowController(engine)
    ipc = IpcServer(SOCKET_NAME)
    ipc.toggleRequested.connect(controller.toggle)

    # If we were launched as a fresh primary (no one to toggle), show the
    # window immediately. This way `jbox` from a keybind "just works" on
    # the very first run, when no daemon is running yet.
    controller.show()

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
