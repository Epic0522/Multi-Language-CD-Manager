#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cdmanager::application {

struct EncodedText {
    bool ok {false};
    QByteArray bytes;
    QString errorMessage;
};

struct ValidationIssue {
    QString fieldLabel;
    QString message;
};

struct ValidationReport {
    bool ok {true};
    QVector<ValidationIssue> issues;

    QString summary() const {
        if (ok) {
            return QStringLiteral("All CD-TEXT fields can be encoded as MS-JIS.");
        }

        QStringList lines;
        for (const ValidationIssue& issue : issues) {
            lines.append(QStringLiteral("%1: %2").arg(issue.fieldLabel, issue.message));
        }
        return lines.join(u'\n');
    }
};

}  // namespace cdmanager::application
