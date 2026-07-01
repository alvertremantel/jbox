#pragma once

#include <QJsonArray>
#include <QString>

namespace JsonStore {

// Writes `payload` to `<path>.tmp` in the same directory, then renames it
// over `path`. Crash-safe: a killed process leaves the old file (or a
// leftover .tmp), never a half-written target.
bool atomicWrite(const QString &path, const QByteArray &payload);

// Reads a JSON array from `path`. Returns `fallback` if the file doesn't
// exist. On a parse or read error, quarantines the bad file by renaming it
// to `<path>.corrupt` (removing any previous .corrupt first) and returns
// `fallback` — never throws.
QJsonArray readJsonArray(const QString &path, const QJsonArray &fallback = {});

}
