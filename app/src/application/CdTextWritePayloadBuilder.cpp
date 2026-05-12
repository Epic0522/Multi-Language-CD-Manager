#include "cdmanager/application/CdTextWritePayloadBuilder.h"

namespace cdmanager::application {

namespace {

bool isWritableAction(CdTextWriteAction action) {
    return action == CdTextWriteAction::WriteEncodedBytes ||
           action == CdTextWriteAction::ReuseImportedBytes;
}

CdTextWritePayloadField makePayloadField(const CdTextWritePlanEntry& entry) {
    return {
        entry.preparedField,
        entry.action,
        entry.reason,
    };
}

CdTextWritePayloadTrack& ensureTrackPayload(CdTextWritePayload& payload, int trackNumber) {
    for (auto& track : payload.tracks) {
        if (track.trackNumber == trackNumber) {
            return track;
        }
    }

    CdTextWritePayloadTrack trackPayload;
    trackPayload.trackNumber = trackNumber;
    payload.tracks.append(trackPayload);
    return payload.tracks.last();
}

}  // namespace

int CdTextWritePayload::writableFieldCount() const {
    int count = albumWritableFields.size();
    for (const auto& track : tracks) {
        count += track.writableFields.size();
    }
    return count;
}

int CdTextWritePayload::skippedFieldCount() const {
    int count = albumSkippedFields.size();
    for (const auto& track : tracks) {
        count += track.skippedFields.size();
    }
    return count;
}

int CdTextWritePayload::writableByteCount() const {
    int total = 0;
    for (const auto& field : albumWritableFields) {
        total += field.preparedField.encodedBytes.size();
    }
    for (const auto& track : tracks) {
        for (const auto& field : track.writableFields) {
            total += field.preparedField.encodedBytes.size();
        }
    }
    return total;
}

CdTextWritePayload CdTextWritePayloadBuilder::build(const CdTextWritePlan& plan) const {
    CdTextWritePayload payload;

    for (const auto& entry : plan.entries) {
        const auto payloadField = makePayloadField(entry);
        const auto trackNumber = entry.preparedField.field.trackNumber;

        if (!trackNumber.has_value()) {
            if (isWritableAction(entry.action)) {
                payload.albumWritableFields.append(payloadField);
            } else {
                payload.albumSkippedFields.append(payloadField);
            }
            continue;
        }

        auto& trackPayload = ensureTrackPayload(payload, *trackNumber);
        if (isWritableAction(entry.action)) {
            trackPayload.writableFields.append(payloadField);
        } else {
            trackPayload.skippedFields.append(payloadField);
        }
    }

    return payload;
}

}  // namespace cdmanager::application
