#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace Terminal {

struct Candidate {
    QString exe;
    QStringList leadArgs;
};

// Finds the first available terminal emulator on PATH, trying
// x-terminal-emulator first (Debian/Ubuntu alternatives symlink, assumed
// "-e"), then a fixed candidate list in a specific order that matters —
// it determines which terminal a user's existing keybind ends up spawning.
// Computed once and cached for the process's lifetime.
std::optional<Candidate> find();

// Launches `command` inside the detected terminal via `shell <args>
// command`, fully detached. Returns false if no terminal was found or the
// spawn failed.
bool launch(const QString &command, const QString &shell);

// Builds the argument list (excluding the shell binary itself) to invoke
// `command` through `shell` as a login shell, with bash-style aliases (e.g.
// from ~/.bashrc) resolved.
//
// For bash specifically, this means `-O expand_aliases -lc command` rather
// than `-lc command` with `shopt -s expand_aliases` prefixed to `command`
// itself: bash parses a whole `-c` string before executing any of it, so a
// `shopt -s expand_aliases;` statement at the start of that same string is
// too late to affect alias expansion for the rest of the string. `-O` sets
// the shopt option before parsing begins, which does work. Other shells
// don't gate alias expansion this way, so they get plain `-lc command`.
QStringList commandArgs(const QString &shell, const QString &command);

}
