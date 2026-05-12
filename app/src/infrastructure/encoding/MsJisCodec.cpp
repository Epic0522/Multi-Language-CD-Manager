#include "cdmanager/infrastructure/encoding/MsJisCodec.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace cdmanager::infrastructure::encoding {

namespace {

#ifdef _WIN32
cdmanager::application::EncodedText encodeWithWindows932(const QString& text) {
    cdmanager::application::EncodedText result;

    BOOL usedDefaultChar = FALSE;
    const int size = WideCharToMultiByte(
        932,
        WC_NO_BEST_FIT_CHARS,
        reinterpret_cast<LPCWCH>(text.utf16()),
        text.size(),
        nullptr,
        0,
        nullptr,
        &usedDefaultChar
    );

    if (size <= 0) {
        result.errorMessage = QStringLiteral("Windows code page 932 conversion failed.");
        return result;
    }

    QByteArray buffer(size, Qt::Uninitialized);
    const int written = WideCharToMultiByte(
        932,
        WC_NO_BEST_FIT_CHARS,
        reinterpret_cast<LPCWCH>(text.utf16()),
        text.size(),
        buffer.data(),
        buffer.size(),
        nullptr,
        &usedDefaultChar
    );

    if (written <= 0) {
        result.errorMessage = QStringLiteral("Windows code page 932 conversion failed.");
        return result;
    }

    if (usedDefaultChar) {
        result.errorMessage = QStringLiteral("Contains characters that cannot be represented in MS-JIS.");
        return result;
    }

    result.ok = true;
    result.bytes = buffer;
    return result;
}
#else
cdmanager::application::EncodedText encodeWithIconv(const QString& text) {
    cdmanager::application::EncodedText result;
    iconv_t handle = iconv_open("SHIFT_JIS", "UTF-8");
    if (handle == reinterpret_cast<iconv_t>(-1)) {
        result.errorMessage = QStringLiteral("iconv could not open SHIFT_JIS encoder.");
        return result;
    }

    QByteArray input = text.toUtf8();
    QByteArray output(input.size() * 4 + 16, '\0');
    char* inputPtr = input.data();
    size_t inputLeft = static_cast<size_t>(input.size());
    char* outputPtr = output.data();
    size_t outputLeft = static_cast<size_t>(output.size());

    const size_t status = iconv(handle, &inputPtr, &inputLeft, &outputPtr, &outputLeft);
    iconv_close(handle);

    if (status == static_cast<size_t>(-1) || inputLeft != 0) {
        if (errno == EILSEQ || errno == EINVAL) {
            result.errorMessage = QStringLiteral("Contains characters that cannot be represented in MS-JIS.");
        } else {
            result.errorMessage = QStringLiteral("SHIFT_JIS conversion failed.");
        }
        return result;
    }

    output.resize(static_cast<int>(output.size() - outputLeft));
    result.ok = true;
    result.bytes = output;
    return result;
}
#endif

}  // namespace

cdmanager::application::EncodedText MsJisCodec::encode(const QString& text) const {
#ifdef _WIN32
    return encodeWithWindows932(text);
#else
    return encodeWithIconv(text);
#endif
}

}  // namespace cdmanager::infrastructure::encoding
