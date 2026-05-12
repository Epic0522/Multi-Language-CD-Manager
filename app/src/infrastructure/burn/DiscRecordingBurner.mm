#include "cdmanager/infrastructure/burn/DiscRecordingBurner.h"

#import <CoreFoundation/CoreFoundation.h>
#import <DiscRecording/DRBurn.h>
#import <DiscRecording/DRCDText.h>
#import <DiscRecording/DRCoreBurn.h>
#import <DiscRecording/DRCoreCDText.h>
#import <DiscRecording/DRCoreDevice.h>
#import <DiscRecording/DRCoreTrack.h>
#import <DiscRecording/DRContentTrack.h>
#import <DiscRecording/DRDevice.h>
#import <DiscRecording/DRTrack.h>
#import <DiscRecording/DRStatus.h>
#import <DiscRecording/DRTrack_ContentSupport.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cdmanager::infrastructure::burn {

// ---------- helpers ----------

static CFStringRef qStringToCF(const QString& s) {
    QByteArray u8 = s.toUtf8();
    return CFStringCreateWithCString(kCFAllocatorDefault, u8.constData(),
                                     kCFStringEncodingUTF8);
}

static NSData* packsToNSData(const QVector<cdmanager::application::burn::CdTextPack>& packs) {
    QByteArray raw;
    raw.reserve(packs.size() * cdmanager::application::burn::kPackTotalSize);
    for (const auto& pack : packs) {
        raw.append(reinterpret_cast<const char*>(pack.data.data()),
                   cdmanager::application::burn::kPackTotalSize);
    }
    return [NSData dataWithBytes:raw.constData() length:(NSUInteger)raw.size()];
}

static QString nsStringToQString(NSString* string) {
    if (string == nil) {
        return {};
    }
    return QString::fromUtf8([string UTF8String]);
}

static QString nsObjectToQString(id object) {
    if (object == nil) {
        return {};
    }
    return QString::fromUtf8([[object description] UTF8String]);
}

struct AudioDataRange {
    bool ok = false;
    quint64 dataOffset = 0;
    quint64 dataLength = 0;
    QString container;
};

static quint32 readBig32(const char* p) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(p));
}

static quint32 readLittle32(const char* p) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(p));
}

static AudioDataRange probeAiffDataRange(const QString& path) {
    AudioDataRange range;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return range;
    }

    const QByteArray data = file.readAll();
    if (data.size() < 12) {
        return range;
    }
    const char* bytes = data.constData();
    if (memcmp(bytes, "FORM", 4) != 0) {
        return range;
    }
    if (memcmp(bytes + 8, "AIFF", 4) != 0 && memcmp(bytes + 8, "AIFC", 4) != 0) {
        return range;
    }

    int offset = 12;
    while (offset + 8 <= data.size()) {
        const char* chunk = bytes + offset;
        const quint32 chunkSize = readBig32(chunk + 4);
        const int payloadOffset = offset + 8;
        if (payloadOffset > data.size()) {
            break;
        }

        if (memcmp(chunk, "SSND", 4) == 0 && chunkSize >= 8 && payloadOffset + 8 <= data.size()) {
            const quint32 soundOffset = readBig32(bytes + payloadOffset);
            const quint32 payloadSize = chunkSize - 8;
            const quint64 dataOffset = static_cast<quint64>(payloadOffset + 8) + soundOffset;
            if (dataOffset <= static_cast<quint64>(data.size())) {
                const quint64 fileRemaining = static_cast<quint64>(data.size()) - dataOffset;
                range.ok = true;
                range.dataOffset = dataOffset;
                range.dataLength = std::min<quint64>(payloadSize, fileRemaining);
                range.container = QStringLiteral("AIFF");
                return range;
            }
            return range;
        }

        int advance = 8 + static_cast<int>(chunkSize);
        if (chunkSize & 1U) {
            ++advance;
        }
        if (advance <= 0) {
            break;
        }
        offset += advance;
    }

    return range;
}

static AudioDataRange probeWaveDataRange(const QString& path) {
    AudioDataRange range;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return range;
    }

    const QByteArray data = file.readAll();
    if (data.size() < 12) {
        return range;
    }
    const char* bytes = data.constData();
    if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0) {
        return range;
    }

    int offset = 12;
    while (offset + 8 <= data.size()) {
        const char* chunk = bytes + offset;
        const quint32 chunkSize = readLittle32(chunk + 4);
        const int payloadOffset = offset + 8;
        if (payloadOffset > data.size()) {
            break;
        }

        if (memcmp(chunk, "data", 4) == 0) {
            range.ok = true;
            range.dataOffset = payloadOffset;
            range.dataLength = std::min<quint64>(chunkSize, static_cast<quint64>(data.size() - payloadOffset));
            range.container = QStringLiteral("WAVE");
            return range;
        }

        int advance = 8 + static_cast<int>(chunkSize);
        if (chunkSize & 1U) {
            ++advance;
        }
        if (advance <= 0) {
            break;
        }
        offset += advance;
    }

    return range;
}

static AudioDataRange probeAudioDataRange(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("aiff") || suffix == QStringLiteral("aif") || suffix == QStringLiteral("aifc")) {
        return probeAiffDataRange(path);
    }
    if (suffix == QStringLiteral("wav") || suffix == QStringLiteral("wave")) {
        return probeWaveDataRange(path);
    }
    return {};
}

}  // namespace cdmanager::infrastructure::burn

@interface CDRawPCMTrackProducer : NSObject <DRTrackDataProduction>
- (instancetype)initWithPath:(NSString*)path
                  dataOffset:(uint64_t)dataOffset
                  dataLength:(uint64_t)dataLength;
@end

@implementation CDRawPCMTrackProducer {
    NSString* _path;
    uint64_t _dataOffset;
    uint64_t _dataLength;
    uint64_t _sectorCount;
    int _fd;
    void* _mappedBase;
    size_t _mappedLength;
}

- (instancetype)initWithPath:(NSString*)path
                  dataOffset:(uint64_t)dataOffset
                  dataLength:(uint64_t)dataLength
{
    self = [super init];
    if (self != nil) {
        _path = [path copy];
        _dataOffset = dataOffset;
        _dataLength = dataLength;
        _sectorCount = dataLength / 2352ULL;
        _fd = -1;
        _mappedBase = MAP_FAILED;
        _mappedLength = 0;
    }
    return self;
}

- (uint64_t)estimateLengthOfTrack:(DRTrack*)track {
    (void)track;
    return _sectorCount;
}

- (BOOL)prepareTrack:(DRTrack*)track forBurn:(DRBurn*)burn toMedia:(NSDictionary*)mediaInfo {
    (void)track;
    (void)burn;
    (void)mediaInfo;
    if (_fd >= 0 && _mappedBase != MAP_FAILED) {
        return YES;
    }

    _fd = open(_path.fileSystemRepresentation, O_RDONLY);
    if (_fd < 0) {
        return NO;
    }

    struct stat st;
    if (fstat(_fd, &st) != 0) {
        close(_fd);
        _fd = -1;
        return NO;
    }

    _mappedLength = static_cast<size_t>(st.st_size);
    _mappedBase = mmap(nullptr, _mappedLength, PROT_READ, MAP_PRIVATE, _fd, 0);
    if (_mappedBase == MAP_FAILED) {
        close(_fd);
        _fd = -1;
        _mappedLength = 0;
        return NO;
    }
    return YES;
}

- (void)cleanupTrackAfterBurn:(DRTrack*)track {
    (void)track;
    if (_mappedBase != MAP_FAILED) {
        munmap(_mappedBase, _mappedLength);
        _mappedBase = MAP_FAILED;
        _mappedLength = 0;
    }
    if (_fd >= 0) {
        close(_fd);
        _fd = -1;
    }
}

- (uint32_t)produceDataForTrack:(DRTrack*)track
                     intoBuffer:(char*)buffer
                         length:(uint32_t)bufferLength
                      atAddress:(uint64_t)address
                      blockSize:(uint32_t)blockSize
                        ioFlags:(uint32_t*)flags
{
    (void)track;
    if (_mappedBase == MAP_FAILED || blockSize == 0) {
        return 0;
    }

    const bool subchannelRequested =
        flags != nullptr && ((*flags & DRFlagSubchannelDataRequested) == DRFlagSubchannelDataRequested);
    const uint32_t userDataBlockSize = subchannelRequested && blockSize >= 96 ? (blockSize - 96) : blockSize;
    if (userDataBlockSize == 0) {
        return 0;
    }

    const uint64_t byteOffset = address * static_cast<uint64_t>(userDataBlockSize);
    if (byteOffset >= _dataLength) {
        return 0;
    }

    const uint64_t remaining = _dataLength - byteOffset;
    const uint32_t maxBlocksByBuffer = bufferLength / blockSize;
    if (maxBlocksByBuffer == 0) {
        return 0;
    }

    const uint64_t remainingBlocks = remaining / userDataBlockSize;
    const uint32_t blocksToCopy = static_cast<uint32_t>(
        std::min<uint64_t>(remainingBlocks, static_cast<uint64_t>(maxBlocksByBuffer)));
    if (blocksToCopy == 0) {
        return 0;
    }

    const char* source = static_cast<const char*>(_mappedBase) + _dataOffset + byteOffset;
    if (!subchannelRequested) {
        const uint32_t bytesToCopy = blocksToCopy * userDataBlockSize;
        memcpy(buffer, source, bytesToCopy);
        return bytesToCopy;
    }

    for (uint32_t blockIndex = 0; blockIndex < blocksToCopy; ++blockIndex) {
        char* destinationBlock = buffer + (static_cast<size_t>(blockIndex) * blockSize);
        const char* sourceBlock = source + (static_cast<size_t>(blockIndex) * userDataBlockSize);
        memcpy(destinationBlock, sourceBlock, userDataBlockSize);
        memset(destinationBlock + userDataBlockSize, 0, 96);
    }

    return blocksToCopy * blockSize;
}

- (uint32_t)producePreGapForTrack:(DRTrack*)track
                       intoBuffer:(char*)buffer
                           length:(uint32_t)bufferLength
                        atAddress:(uint64_t)address
                        blockSize:(uint32_t)blockSize
                          ioFlags:(uint32_t*)flags
{
    (void)track;
    (void)address;
    (void)blockSize;
    (void)flags;
    memset(buffer, 0, bufferLength);
    return bufferLength;
}

- (BOOL)prepareTrackForVerification:(DRTrack*)track {
    (void)track;
    return YES;
}

- (BOOL)verifyPreGapForTrack:(DRTrack*)track
                    inBuffer:(const char*)buffer
                      length:(uint32_t)bufferLength
                   atAddress:(uint64_t)address
                   blockSize:(uint32_t)blockSize
                     ioFlags:(uint32_t*)flags
{
    (void)track;
    (void)buffer;
    (void)bufferLength;
    (void)address;
    (void)blockSize;
    (void)flags;
    return YES;
}

- (BOOL)verifyDataForTrack:(DRTrack*)track
                  inBuffer:(const char*)buffer
                    length:(uint32_t)bufferLength
                 atAddress:(uint64_t)address
                 blockSize:(uint32_t)blockSize
                   ioFlags:(uint32_t*)flags
{
    (void)track;
    (void)buffer;
    (void)bufferLength;
    (void)address;
    (void)blockSize;
    (void)flags;
    return YES;
}

- (BOOL)cleanupTrackAfterVerification:(DRTrack*)track {
    (void)track;
    return YES;
}
@end

namespace cdmanager::infrastructure::burn {

static QString burnStateLabel(NSString* state, NSNumber* currentTrack, NSNumber* totalTracks) {
    if (state == nil) {
        return QStringLiteral("Burning");
    }
    if ([state isEqualToString:DRStatusStatePreparing]) {
        return QStringLiteral("Preparing");
    }
    if ([state isEqualToString:DRStatusStateTrackOpen]) {
        return QStringLiteral("Opening track");
    }
    if ([state isEqualToString:DRStatusStateTrackWrite]) {
        if (currentTrack != nil && totalTracks != nil) {
            return QStringLiteral("Writing track %1/%2")
                .arg(currentTrack.intValue)
                .arg(totalTracks.intValue);
        }
        if (currentTrack != nil) {
            return QStringLiteral("Writing track %1").arg(currentTrack.intValue);
        }
        return QStringLiteral("Writing track");
    }
    if ([state isEqualToString:DRStatusStateTrackClose]) {
        return QStringLiteral("Closing track");
    }
    if ([state isEqualToString:DRStatusStateSessionOpen]) {
        return QStringLiteral("Opening session");
    }
    if ([state isEqualToString:DRStatusStateSessionClose]) {
        return QStringLiteral("Closing session");
    }
    if ([state isEqualToString:DRStatusStateFinishing]) {
        return QStringLiteral("Finishing");
    }
    if ([state isEqualToString:DRStatusStateVerifying]) {
        return QStringLiteral("Verifying");
    }
    if ([state isEqualToString:DRStatusStateDone]) {
        return QStringLiteral("Done");
    }
    if ([state isEqualToString:DRStatusStateFailed]) {
        return QStringLiteral("Failed");
    }
    return nsStringToQString(state);
}

static QString burnErrorSummary(NSDictionary* status) {
    NSDictionary* errorDict = status[DRErrorStatusKey];
    if (![errorDict isKindOfClass:[NSDictionary class]]) {
        return {};
    }

    NSString* message = errorDict[DRErrorStatusErrorStringKey];
    NSString* info = errorDict[DRErrorStatusErrorInfoStringKey];
    if (message != nil && info != nil && ![info isEqualToString:@""]) {
        return QStringLiteral("%1 (%2)").arg(nsStringToQString(message), nsStringToQString(info));
    }
    if (message != nil) {
        return nsStringToQString(message);
    }
    return {};
}

// ---------- Impl ----------

struct DiscRecordingBurner::Impl {
    bool simulationMode = true;
    int burnSpeed = 0;
    ProgressCb progressCb;
};

DiscRecordingBurner::DiscRecordingBurner()
    : m_impl(std::make_unique<Impl>()) {}

DiscRecordingBurner::~DiscRecordingBurner() = default;

void DiscRecordingBurner::setSimulationMode(bool on) { m_impl->simulationMode = on; }
void DiscRecordingBurner::setBurnSpeed(int speedX) { m_impl->burnSpeed = speedX; }
void DiscRecordingBurner::setProgressCallback(ProgressCb cb) { m_impl->progressCb = std::move(cb); }

DiscRecordingCdTextAnalysis DiscRecordingBurner::analyzeCdTextPacks(
    const QVector<cdmanager::application::burn::CdTextPack>& cdTextPacks)
{
    DiscRecordingCdTextAnalysis analysis;
    analysis.diagnostics = QStringLiteral("DiscRecording CD-TEXT analysis\nPack count: %1")
        .arg(cdTextPacks.size());

    if (cdTextPacks.isEmpty()) {
        analysis.ok = true;
        analysis.diagnostics += QStringLiteral("\nResult: no CD-TEXT packs to analyze");
        return analysis;
    }

    NSData* packData = packsToNSData(cdTextPacks);
    if (packData == nil) {
        analysis.error = QStringLiteral("Could not allocate raw CD-TEXT pack blob.");
        analysis.diagnostics += QStringLiteral("\nResult: failed to allocate pack blob");
        return analysis;
    }

    NSArray* textBlocks = [DRCDTextBlock arrayOfCDTextBlocksFromPacks:packData];

    if (textBlocks == nil) {
        analysis.error = QStringLiteral("DiscRecording rejected the current CD-TEXT pack blob.");
        analysis.diagnostics += QStringLiteral("\nResult: DRCDTextBlock array parser returned nil");
        return analysis;
    }

    analysis.blockCount = static_cast<int>(textBlocks.count);
    if (analysis.blockCount <= 0) {
        analysis.error = QStringLiteral("DiscRecording created zero CD-TEXT blocks from the current pack blob.");
        analysis.diagnostics += QStringLiteral("\nResult: zero CD-TEXT blocks");
        return analysis;
    }

    analysis.ok = true;
    analysis.diagnostics += QStringLiteral("\nBlock count: %1\nResult: accepted")
        .arg(analysis.blockCount);
    return analysis;
}

BurnResult DiscRecordingBurner::burn(
    const QString& devicePath,
    const QVector<cdmanager::application::burn::CdTextPack>& cdTextPacks,
    const QStringList& audioFiles)
{
    BurnResult result;
    result.diagnostics = QStringLiteral("Backend: DiscRecording\nDevice: %1\nTracks: %2\nCD-TEXT packs: %3\nSimulation: %4")
        .arg(devicePath)
        .arg(audioFiles.size())
        .arg(cdTextPacks.size())
        .arg(m_impl->simulationMode ? QStringLiteral("yes") : QStringLiteral("no"));

    // ---- Find device by BSD name ----
    qDebug() << "Burn: devicePath =" << devicePath;
    if (devicePath.isEmpty()) {
        result.error = QStringLiteral("No device path set.");
        return result;
    }

    QString bsdName;
    if (devicePath.startsWith(QStringLiteral("/dev/rdisk"))) {
        bsdName = QStringLiteral("disk") + devicePath.mid(10);
    } else if (devicePath.startsWith(QStringLiteral("/dev/disk"))) {
        bsdName = devicePath.mid(5);
    } else {
        bsdName = devicePath;
    }
    qDebug() << "Burn: bsdName =" << bsdName;

    CFStringRef bsdCF = qStringToCF(bsdName);
    DRDeviceRef targetDevice = DRDeviceCopyDeviceForBSDName(bsdCF);
    CFRelease(bsdCF);
    qDebug() << "Burn: targetDevice =" << (targetDevice != nullptr ? "found" : "NULL");

    if (targetDevice == nullptr || !DRDeviceIsValid(targetDevice)) {
        result.error = QStringLiteral("No CD burner found for: %1").arg(devicePath);
        result.diagnostics += QStringLiteral("\nResult: invalid target device");
        if (targetDevice) CFRelease(targetDevice);
        return result;
    }

    DRDevice* deviceObject = (__bridge DRDevice*)targetDevice;
    NSDictionary* deviceInfo = [deviceObject info];
    NSDictionary* deviceStatus = [deviceObject status];
    result.diagnostics += QStringLiteral("\nDevice info: %1").arg(nsObjectToQString(deviceInfo));
    result.diagnostics += QStringLiteral("\nDevice status: %1").arg(nsObjectToQString(deviceStatus));

    const bool exclusiveAccess = [deviceObject acquireExclusiveAccess];
    result.diagnostics += QStringLiteral("\nExclusive access: %1")
        .arg(exclusiveAccess ? QStringLiteral("acquired") : QStringLiteral("not acquired"));

    // ---- Create audio tracks ----
    qDebug() << "Burn: creating" << audioFiles.size() << "audio tracks...";
    NSMutableArray<DRTrack*>* tracks = [[NSMutableArray alloc] initWithCapacity:audioFiles.size()];
    for (const QString& audioFile : audioFiles) {
        qDebug() << "Burn: adding track:" << audioFile;
        QFileInfo fi(audioFile);
        if (!fi.exists() || fi.size() == 0) {
            result.error = QStringLiteral("Missing prepared audio file: %1").arg(audioFile);
            result.diagnostics += QStringLiteral("\nResult: missing prepared audio file -> %1").arg(audioFile);
            if (exclusiveAccess) {
                [deviceObject releaseExclusiveAccess];
            }
            CFRelease(targetDevice);
            return result;
        }

        const AudioDataRange audioRange = probeAudioDataRange(fi.absoluteFilePath());
        result.diagnostics += QStringLiteral("\nAudio data range: file=%1 container=%2 offset=%3 bytes=%4")
            .arg(audioFile)
            .arg(audioRange.container.isEmpty() ? QStringLiteral("(unknown)") : audioRange.container)
            .arg(static_cast<qulonglong>(audioRange.dataOffset))
            .arg(static_cast<qulonglong>(audioRange.dataLength));
        if (!audioRange.ok || audioRange.dataLength == 0 || (audioRange.dataLength % 2352ULL) != 0ULL) {
            result.error = QStringLiteral("Prepared audio file is not a usable raw PCM track source: %1")
                .arg(fi.fileName());
            result.diagnostics += QStringLiteral(
                "\nResult: audio data range probe failed or data length is not sector-aligned");
            if (exclusiveAccess) {
                [deviceObject releaseExclusiveAccess];
            }
            CFRelease(targetDevice);
            return result;
        }

        const QByteArray audioPathUtf8 = fi.absoluteFilePath().toUtf8();
        NSString* audioPathNSString = [NSString stringWithUTF8String:audioPathUtf8.constData()];
        DRTrack* trackObject = [DRTrack trackForAudioFile:audioPathNSString];
        QString trackSource = QStringLiteral("trackForAudioFile");

        if (trackObject == nil) {
            result.diagnostics += QStringLiteral(
                "\nTrack source: trackForAudioFile returned nil, falling back to custom producer");
            CDRawPCMTrackProducer* producer =
                [[CDRawPCMTrackProducer alloc] initWithPath:audioPathNSString
                                                 dataOffset:audioRange.dataOffset
                                                 dataLength:audioRange.dataLength];
            DRMSF* trackLength = [DRMSF msfWithFrames:static_cast<UInt32>(audioRange.dataLength / 2352ULL)];
            trackObject = [DRTrack trackForAudioOfLength:trackLength producer:producer];
            trackSource = QStringLiteral("trackForAudioOfLength:producer:");
        }
        if (trackObject == nil) {
            result.error = QStringLiteral("Could not create DiscRecording audio track: %1").arg(audioFile);
            result.diagnostics += QStringLiteral("\nResult: audio track creation failed -> %1").arg(audioFile);
            if (exclusiveAccess) {
                [deviceObject releaseExclusiveAccess];
            }
            CFRelease(targetDevice);
            return result;
        }

        NSMutableDictionary* trackProperties = [[NSMutableDictionary alloc] init];
        trackProperties[DRPreGapLengthKey] = @150;
        trackProperties[DRVerificationTypeKey] = DRVerificationTypeNone;
        [trackObject setProperties:trackProperties];

        const uint64_t estimatedLength = [trackObject estimateLength];
        const float productionSpeed = [trackObject testProductionSpeedForInterval:0.25];
        NSDictionary* resolvedTrackProperties = [trackObject properties];
        result.diagnostics += QStringLiteral(
            "\nTrack preflight: file=%1 source=%2 estimateLength=%3 productionSpeed=%4 props=%5")
            .arg(audioFile)
            .arg(trackSource)
            .arg(static_cast<qulonglong>(estimatedLength))
            .arg(productionSpeed, 0, 'f', 2)
            .arg(nsObjectToQString(resolvedTrackProperties));
        if (estimatedLength == 0 || productionSpeed <= 0.f) {
            result.error = QStringLiteral("DiscRecording could not preflight the prepared audio track: %1")
                .arg(fi.fileName());
            result.diagnostics += QStringLiteral("\nResult: audio track producer preflight failed");
            if (exclusiveAccess) {
                [deviceObject releaseExclusiveAccess];
            }
            CFRelease(targetDevice);
            return result;
        }

        [tracks addObject:trackObject];
    }
    const CFIndex trackCount = static_cast<CFIndex>(tracks.count);
    qDebug() << "Burn:" << trackCount << "tracks created.";
    qDebug() << "Burn: checking track count...";

    if (trackCount == 0) {
        result.error = QStringLiteral("No valid audio tracks.");
        result.diagnostics += QStringLiteral("\nResult: no valid audio tracks");
        if (exclusiveAccess) {
            [deviceObject releaseExclusiveAccess];
        }
        CFRelease(targetDevice);
        return result;
    }

    // ---- Create burn object ----
    DRBurn* burn = [DRBurn burnForDevice:deviceObject];
    if (burn == nil) {
        result.error = QStringLiteral("Could not create DRBurn.");
        result.diagnostics += QStringLiteral("\nResult: could not create DRBurn");
        if (exclusiveAccess) {
            [deviceObject releaseExclusiveAccess];
        }
        CFRelease(targetDevice);
        return result;
    }

    NSMutableDictionary* burnProps = [[NSMutableDictionary alloc] init];

    // Simulation mode.
    burnProps[(__bridge NSString*)kDRBurnTestingKey] = m_impl->simulationMode ? @YES : @NO;

    // Write speed.
    if (m_impl->burnSpeed > 0) {
        burnProps[(__bridge NSString*)kDRBurnRequestedSpeedKey] = @(static_cast<float>(m_impl->burnSpeed));
    }

    // Eject after burn.
    burnProps[(__bridge NSString*)kDRBurnCompletionActionKey]
        = (__bridge NSString*)kDRBurnCompletionActionEject;

    // ---- CD-TEXT ----
    NSArray* textBlocks = nil;
    if (!cdTextPacks.isEmpty()) {
        NSData* packData = packsToNSData(cdTextPacks);
        textBlocks = [DRCDTextBlock arrayOfCDTextBlocksFromPacks:packData];

        if (textBlocks != nil && textBlocks.count > 0) {
            // kDRCDTextKey accepts a single block or array of blocks.
            if (textBlocks.count == 1) {
                burnProps[(__bridge NSString*)kDRCDTextKey] = textBlocks.firstObject;
            } else {
                burnProps[(__bridge NSString*)kDRCDTextKey] = textBlocks;
            }
            result.diagnostics += QStringLiteral("\nCD-TEXT block count: %1")
                .arg(static_cast<int>(textBlocks.count));
        } else {
            result.error = QStringLiteral("DiscRecording rejected the current CD-TEXT pack blob.");
            result.diagnostics += QStringLiteral("\nResult: DRCDTextBlock array parser returned nil or empty");
            if (exclusiveAccess) {
                [deviceObject releaseExclusiveAccess];
            }
            CFRelease(targetDevice);
            return result;
        }
    }

    [burn setProperties:burnProps];

    // ---- Burn ----
    if (m_impl->progressCb) {
        BurnProgress bp;
        bp.phase = QStringLiteral("Starting burn");
        bp.trackCount = static_cast<int>(audioFiles.size());
        bp.overallPercent = 1.f;
        m_impl->progressCb(bp);
    }

    qDebug() << "Burn: simulation =" << m_impl->simulationMode
             << "tracks =" << audioFiles.size()
             << "cdTextBlocks =" << (textBlocks != nil ? (int)textBlocks.count : 0);

    qDebug() << "Burn: calling DRBurn writeLayout...";
    @try {
        id layout = tracks.count == 1 ? tracks.firstObject : tracks;
        [burn writeLayout:layout];
    } @catch (NSException* exception) {
        result.error = QStringLiteral("DiscRecording threw while starting burn: %1").arg(nsStringToQString(exception.reason));
        result.diagnostics += QStringLiteral("\nResult: exception while starting burn\nException: %1")
            .arg(nsObjectToQString(exception));
        qDebug() << "Burn failed:" << result.error;
        if (m_impl->progressCb) {
            BurnProgress bp;
            bp.phase = QStringLiteral("Failed");
            bp.overallPercent = 100.f;
            m_impl->progressCb(bp);
        }
        if (exclusiveAccess) {
            [deviceObject releaseExclusiveAccess];
        }
        CFRelease(targetDevice);
        return result;
    }

    result.diagnostics += QStringLiteral("\nResult: burn job started; waiting for DiscRecording completion status");

    QElapsedTimer timer;
    timer.start();
    QString lastState;
    QString lastPhase;
    double lastPercent = -1.0;
    constexpr qint64 timeoutMs = 3LL * 60LL * 60LL * 1000LL;

    while (timer.elapsed() < timeoutMs) {
        NSDictionary* status = [burn status];
        if (![status isKindOfClass:[NSDictionary class]]) {
            QCoreApplication::processEvents();
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
            continue;
        }

        NSString* state = status[DRStatusStateKey];
        NSNumber* percentNumber = status[DRStatusPercentCompleteKey];
        NSNumber* currentTrack = status[DRStatusCurrentTrackKey];
        NSNumber* totalTracks = status[DRStatusTotalTracksKey];

        const QString stateText = nsStringToQString(state);
        const QString phase = burnStateLabel(state, currentTrack, totalTracks);
        const double percent = [percentNumber isKindOfClass:[NSNumber class]]
            ? percentNumber.doubleValue
            : -1.0;

        if (stateText != lastState || phase != lastPhase || !qFuzzyCompare(percent + 1.0, lastPercent + 1.0)) {
            result.diagnostics += QStringLiteral("\nStatus: state=%1 phase=%2 percent=%3")
                .arg(stateText.isEmpty() ? QStringLiteral("(null)") : stateText,
                     phase,
                     percent >= 0.0 ? QString::number(percent * 100.0, 'f', 1) : QStringLiteral("(unknown)"));
            lastState = stateText;
            lastPhase = phase;
            lastPercent = percent;
        }

        if (m_impl->progressCb) {
            BurnProgress bp;
            bp.phase = phase;
            bp.trackIndex = [currentTrack isKindOfClass:[NSNumber class]] ? currentTrack.intValue : 0;
            bp.trackCount = [totalTracks isKindOfClass:[NSNumber class]] ? totalTracks.intValue : static_cast<int>(audioFiles.size());
            bp.overallPercent = percent >= 0.0 ? static_cast<float>(percent * 100.0) : 50.f;
            m_impl->progressCb(bp);
        }

        if ([state isEqualToString:DRStatusStateDone]) {
            result.ok = true;
            result.diagnostics += QStringLiteral("\nResult: success");
            qDebug() << "Burn completed successfully.";
            break;
        }

        if ([state isEqualToString:DRStatusStateFailed]) {
            const QString errorSummary = burnErrorSummary(status);
            result.error = !errorSummary.isEmpty()
                ? errorSummary
                : QStringLiteral("DiscRecording burn failed.");
            result.diagnostics += QStringLiteral("\nResult: failure after status polling\nError dict: %1")
                .arg(nsObjectToQString(status[DRErrorStatusKey]));
            qDebug() << "Burn failed after status polling:" << result.error;
            break;
        }

        QCoreApplication::processEvents();
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    }

    if (!result.ok && result.error.isEmpty()) {
        result.error = QStringLiteral("Timed out waiting for DiscRecording burn completion.");
        result.diagnostics += QStringLiteral("\nResult: timeout while waiting for completion");
    }

    if (m_impl->progressCb) {
        BurnProgress bp;
        bp.phase = result.ok ? QStringLiteral("Done") : QStringLiteral("Failed");
        bp.overallPercent = 100.f;
        bp.trackCount = static_cast<int>(audioFiles.size());
        m_impl->progressCb(bp);
    }

    // ---- Cleanup ----
    if (exclusiveAccess) {
        [deviceObject releaseExclusiveAccess];
    }
    CFRelease(targetDevice);

    return result;
}

}  // namespace cdmanager::infrastructure::burn
