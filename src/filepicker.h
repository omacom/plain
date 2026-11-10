#pragma once

#include <QObject>
#include <QUrl>

// Open and save dialogs, behind an interface so tests can inject a fake.
// Follows Omacut's (MIT, David Heinemeier Hansson).
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openFile(const QString &folder) = 0;
    virtual void saveFile(const QString &suggestedPath) = 0;

signals:
    void openSelected(const QUrl &url);
    void saveSelected(const QUrl &url);
    void failed(const QString &message);
};
