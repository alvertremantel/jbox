#!/usr/bin/env bash
# Build jbox and (re)install it for the current user: the binary on PATH
# plus a .desktop entry so it's indexed by KRunner/the app menu like any
# other application. Safe to re-run — it wipes its own previous install
# before copying the fresh build over, so repeated `./install.sh` during
# development never leaves stale files behind.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$script_dir/build"

bin_dir="$HOME/.local/bin"
desktop_dir="$HOME/.local/share/applications"
desktop_file="$desktop_dir/dev.jbox.app.desktop"
installed_bin="$bin_dir/jbox"

if [[ ! -d "$build_dir" ]]; then
    cmake --preset default -S "$script_dir"
fi
cmake --build --preset default

echo "Removing previous install (if any)..."
rm -f "$installed_bin" "$desktop_file"

echo "Installing binary to $installed_bin"
install -Dm755 "$build_dir/jbox" "$installed_bin"

echo "Installing desktop entry to $desktop_file"
install -d "$desktop_dir"
cat > "$desktop_file" <<EOF
[Desktop Entry]
Type=Application
Name=jbox
Comment=A minimal KDE-styled command launcher
Exec=$installed_bin
TryExec=$installed_bin
Icon=utilities-terminal
Terminal=false
Categories=Utility;System;
Keywords=launcher;run;command;terminal;
EOF

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$desktop_dir" 2>/dev/null || true
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

case ":$PATH:" in
    *":$bin_dir:"*) ;;
    *) echo "Note: $bin_dir isn't on your PATH — add it to run 'jbox' directly from a shell." ;;
esac

echo "Done. jbox is installed at $installed_bin and should show up in KRunner/the app menu."
