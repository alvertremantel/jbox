#pragma once

#include <QObject>

// Owns the QML root ApplicationWindow and the show/hide bookkeeping. The
// caller is responsible for verifying `root` is non-null (i.e. the QML
// engine loaded successfully) before constructing this.
class WindowController : public QObject {
    Q_OBJECT

public:
    explicit WindowController(QObject *root, QObject *parent = nullptr);

public slots:
    void toggle();
    void show();
    void hide();

private:
    QObject *m_win;
};
