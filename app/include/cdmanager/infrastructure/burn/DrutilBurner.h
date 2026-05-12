#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace cdmanager::infrastructure::burn {

struct DrutilBurnResult {
    bool ok = false;
    QString stdOut;
    QString stdErr;
    QString error;
};

// Burns an audio CD using macOS drutil burn + cdrdao .toc file.
// The .toc file references WAV files and contains CD-TEXT metadata.
class DrutilBurner : public QObject {
    Q_OBJECT

public:
    explicit DrutilBurner(QObject* parent = nullptr);

    // deviceIndex: numeric drive index (1-based, as used by drutil -drive N).
    // tocFilePath: path to the .toc file.
    // simulation: if true, uses drutil -test mode (laser off).
    DrutilBurnResult burn(int deviceIndex, const QString& tocFilePath,
                          bool simulation,
                          int speedX = 0,
                          bool standardPregap = false);

    static QString findDevicePath();
    static int deviceIndexForPath(const QString& devicePath);
};

}  // namespace cdmanager::infrastructure::burn
