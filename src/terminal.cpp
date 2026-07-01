#include "terminal.h"

#include <QProcess>
#include <QStandardPaths>
#include <array>

namespace Terminal {

namespace {

struct CandidateSpec {
    const char *name;
    std::initializer_list<const char *> leadArgs;
};

// Order matters: this is the exact fallback chain a user's window-manager
// keybind relies on.
constexpr std::array<CandidateSpec, 13> kCandidates{{
    {"konsole", {"-e"}},
    {"gnome-terminal", {"--wait", "--"}},
    {"xfce4-terminal", {"-e"}},
    {"alacritty", {"-e"}},
    {"kitty", {"--"}},
    {"wezterm", {"start", "--"}},
    {"foot", {"--"}},
    {"tilix", {"-e"}},
    {"terminator", {"-e"}},
    {"xterm", {"-e"}},
    {"uxterm", {"-e"}},
    {"rxvt", {"-e"}},
    {"urxvt", {"-e"}},
}};

std::optional<Candidate> findUncached() {
    const QString xTerm = QStandardPaths::findExecutable("x-terminal-emulator");
    if (!xTerm.isEmpty()) {
        return Candidate{xTerm, {"-e"}};
    }

    for (const CandidateSpec &spec : kCandidates) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(spec.name));
        if (!path.isEmpty()) {
            QStringList args;
            for (const char *arg : spec.leadArgs) {
                args << QString::fromLatin1(arg);
            }
            return Candidate{path, args};
        }
    }
    return std::nullopt;
}

}

std::optional<Candidate> find() {
    static const std::optional<Candidate> cached = findUncached();
    return cached;
}

bool launch(const QString &command, const QString &shell) {
    const std::optional<Candidate> term = find();
    if (!term) {
        return false;
    }
    QStringList args = term->leadArgs;
    args << shell << "-lc" << command;
    return QProcess::startDetached(term->exe, args);
}

}
