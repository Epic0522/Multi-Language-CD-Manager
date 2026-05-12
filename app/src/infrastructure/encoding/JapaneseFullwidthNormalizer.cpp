#include "cdmanager/infrastructure/encoding/JapaneseFullwidthNormalizer.h"

#include <QHash>

namespace cdmanager::infrastructure::encoding {

namespace {

const QHash<QChar, QString>& singleCharMap() {
    static const QHash<QChar, QString> map {
        {QChar(0xFF61), QStringLiteral("。")},
        {QChar(0xFF62), QStringLiteral("「")},
        {QChar(0xFF63), QStringLiteral("」")},
        {QChar(0xFF64), QStringLiteral("、")},
        {QChar(0xFF65), QStringLiteral("・")},
        {QChar(0xFF66), QStringLiteral("ヲ")},
        {QChar(0xFF67), QStringLiteral("ァ")},
        {QChar(0xFF68), QStringLiteral("ィ")},
        {QChar(0xFF69), QStringLiteral("ゥ")},
        {QChar(0xFF6A), QStringLiteral("ェ")},
        {QChar(0xFF6B), QStringLiteral("ォ")},
        {QChar(0xFF6C), QStringLiteral("ャ")},
        {QChar(0xFF6D), QStringLiteral("ュ")},
        {QChar(0xFF6E), QStringLiteral("ョ")},
        {QChar(0xFF6F), QStringLiteral("ッ")},
        {QChar(0xFF70), QStringLiteral("ー")},
        {QChar(0xFF71), QStringLiteral("ア")},
        {QChar(0xFF72), QStringLiteral("イ")},
        {QChar(0xFF73), QStringLiteral("ウ")},
        {QChar(0xFF74), QStringLiteral("エ")},
        {QChar(0xFF75), QStringLiteral("オ")},
        {QChar(0xFF76), QStringLiteral("カ")},
        {QChar(0xFF77), QStringLiteral("キ")},
        {QChar(0xFF78), QStringLiteral("ク")},
        {QChar(0xFF79), QStringLiteral("ケ")},
        {QChar(0xFF7A), QStringLiteral("コ")},
        {QChar(0xFF7B), QStringLiteral("サ")},
        {QChar(0xFF7C), QStringLiteral("シ")},
        {QChar(0xFF7D), QStringLiteral("ス")},
        {QChar(0xFF7E), QStringLiteral("セ")},
        {QChar(0xFF7F), QStringLiteral("ソ")},
        {QChar(0xFF80), QStringLiteral("タ")},
        {QChar(0xFF81), QStringLiteral("チ")},
        {QChar(0xFF82), QStringLiteral("ツ")},
        {QChar(0xFF83), QStringLiteral("テ")},
        {QChar(0xFF84), QStringLiteral("ト")},
        {QChar(0xFF85), QStringLiteral("ナ")},
        {QChar(0xFF86), QStringLiteral("ニ")},
        {QChar(0xFF87), QStringLiteral("ヌ")},
        {QChar(0xFF88), QStringLiteral("ネ")},
        {QChar(0xFF89), QStringLiteral("ノ")},
        {QChar(0xFF8A), QStringLiteral("ハ")},
        {QChar(0xFF8B), QStringLiteral("ヒ")},
        {QChar(0xFF8C), QStringLiteral("フ")},
        {QChar(0xFF8D), QStringLiteral("ヘ")},
        {QChar(0xFF8E), QStringLiteral("ホ")},
        {QChar(0xFF8F), QStringLiteral("マ")},
        {QChar(0xFF90), QStringLiteral("ミ")},
        {QChar(0xFF91), QStringLiteral("ム")},
        {QChar(0xFF92), QStringLiteral("メ")},
        {QChar(0xFF93), QStringLiteral("モ")},
        {QChar(0xFF94), QStringLiteral("ヤ")},
        {QChar(0xFF95), QStringLiteral("ユ")},
        {QChar(0xFF96), QStringLiteral("ヨ")},
        {QChar(0xFF97), QStringLiteral("ラ")},
        {QChar(0xFF98), QStringLiteral("リ")},
        {QChar(0xFF99), QStringLiteral("ル")},
        {QChar(0xFF9A), QStringLiteral("レ")},
        {QChar(0xFF9B), QStringLiteral("ロ")},
        {QChar(0xFF9C), QStringLiteral("ワ")},
        {QChar(0xFF9D), QStringLiteral("ン")},
        {QChar(0xFF9E), QStringLiteral("゛")},
        {QChar(0xFF9F), QStringLiteral("゜")}
    };
    return map;
}

const QHash<QString, QString>& voicedPairMap() {
    static const QHash<QString, QString> map {
        {QStringLiteral("ｳﾞ"), QStringLiteral("ヴ")},
        {QStringLiteral("ｶﾞ"), QStringLiteral("ガ")},
        {QStringLiteral("ｷﾞ"), QStringLiteral("ギ")},
        {QStringLiteral("ｸﾞ"), QStringLiteral("グ")},
        {QStringLiteral("ｹﾞ"), QStringLiteral("ゲ")},
        {QStringLiteral("ｺﾞ"), QStringLiteral("ゴ")},
        {QStringLiteral("ｻﾞ"), QStringLiteral("ザ")},
        {QStringLiteral("ｼﾞ"), QStringLiteral("ジ")},
        {QStringLiteral("ｽﾞ"), QStringLiteral("ズ")},
        {QStringLiteral("ｾﾞ"), QStringLiteral("ゼ")},
        {QStringLiteral("ｿﾞ"), QStringLiteral("ゾ")},
        {QStringLiteral("ﾀﾞ"), QStringLiteral("ダ")},
        {QStringLiteral("ﾁﾞ"), QStringLiteral("ヂ")},
        {QStringLiteral("ﾂﾞ"), QStringLiteral("ヅ")},
        {QStringLiteral("ﾃﾞ"), QStringLiteral("デ")},
        {QStringLiteral("ﾄﾞ"), QStringLiteral("ド")},
        {QStringLiteral("ﾊﾞ"), QStringLiteral("バ")},
        {QStringLiteral("ﾋﾞ"), QStringLiteral("ビ")},
        {QStringLiteral("ﾌﾞ"), QStringLiteral("ブ")},
        {QStringLiteral("ﾍﾞ"), QStringLiteral("ベ")},
        {QStringLiteral("ﾎﾞ"), QStringLiteral("ボ")},
        {QStringLiteral("ﾊﾟ"), QStringLiteral("パ")},
        {QStringLiteral("ﾋﾟ"), QStringLiteral("ピ")},
        {QStringLiteral("ﾌﾟ"), QStringLiteral("プ")},
        {QStringLiteral("ﾍﾟ"), QStringLiteral("ペ")},
        {QStringLiteral("ﾎﾟ"), QStringLiteral("ポ")}
    };
    return map;
}

bool isAsciiConvertible(QChar ch) {
    return ch.unicode() == 0x20 || (ch.unicode() >= 0x21 && ch.unicode() <= 0x7E);
}

QChar toFullwidthAscii(QChar ch) {
    if (ch.unicode() == 0x20) {
        return QChar(0x3000);
    }
    return QChar(ch.unicode() + 0xFEE0);
}

}  // namespace

FullwidthNormalizationResult JapaneseFullwidthNormalizer::normalize(const QString& text) const {
    FullwidthNormalizationResult result;
    QString output;
    output.reserve(text.size() * 2);

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];

        if (i + 1 < text.size()) {
            const QString pair = text.mid(i, 2);
            if (const auto it = voicedPairMap().find(pair); it != voicedPairMap().end()) {
                output += it.value();
                result.changed = true;
                ++i;
                continue;
            }
        }

        if (isAsciiConvertible(ch)) {
            output += toFullwidthAscii(ch);
            if (toFullwidthAscii(ch) != ch) {
                result.changed = true;
            }
            continue;
        }

        if (const auto it = singleCharMap().find(ch); it != singleCharMap().end()) {
            output += it.value();
            if (it.value() != QString(ch)) {
                result.changed = true;
            }
            continue;
        }

        output += ch;
    }

    result.normalizedText = output;
    return result;
}

}  // namespace cdmanager::infrastructure::encoding
