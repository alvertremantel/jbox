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

// Launches `command` inside the detected terminal via `shell -lc command`,
// fully detached. Returns false if no terminal was found or the spawn
// failed.
bool launch(const QString &command, const QString &shell);

}
