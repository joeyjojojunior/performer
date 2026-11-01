#include "model/NoteTrack.h"
#include "model/ProjectVersion.h"
#include "model/Settings.h"
#include "model/Scale.h"
#include "ui/controllers/knobpad/StepNoteRange.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main() {
    CHECK(StepNoteRange::absoluteSemitone(0, -2, 3) == -24);
    CHECK(StepNoteRange::absoluteSemitone(127, -2, 3) == 36);
    int previous = -24;
    for (int value = 0; value < 128; ++value) {
        const int note = StepNoteRange::absoluteSemitone(value, -2, 3);
        CHECK(note >= previous && note <= 36);
        previous = note;
    }
    CHECK(StepNoteRange::absoluteSemitone(64, 1, 1) == 12);
    CHECK(StepNoteRange::relativeNote(11, 1, -24, 36) == 12);
    CHECK(StepNoteRange::relativeNote(36, 1, -24, 36) == 36);
    CHECK(StepNoteRange::relativeNote(-24, -1, -24, 36) == -24);
    CHECK(StepNoteRange::relativeNote(50, -1, -24, 36) == 49);
    CHECK(StepNoteRange::relativeNote(50, 1, -24, 36) == 50);
    CHECK(StepNoteRange::relativeNote(-40, 1, -24, 36) == -39);
    CHECK(StepNoteRange::relativeNote(-40, -1, -24, 36) == -40);
    CHECK(StepNoteRange::octaveNote(5, 1, 12, -24, 36) == 17);
    CHECK(StepNoteRange::octaveNote(5, -1, 12, -24, 36) == -7);
    CHECK(StepNoteRange::octaveNote(5, 3, 12, -24, 36) == 29);
    CHECK(StepNoteRange::octaveNote(29, 1, 12, -24, 36) == 29); // no partial octave
    CHECK(StepNoteRange::octaveNote(-19, -1, 12, -24, 36) == -19);
    CHECK(StepNoteRange::octaveNote(50, -1, 12, -24, 36) == 38);
    CHECK(StepNoteRange::octaveNote(50, 1, 12, -24, 36) == 50);
    CHECK(StepNoteRange::octaveNote(5, 0, 12, -24, 36) == 5);

    // Bounds are musical octaves, rather than fixed indices in every scale.
    const auto &major = Scale::get(1);
    CHECK(major.noteFromVolts(-2.f) == -14);
    CHECK(major.noteFromVolts(3.f) == 21);
    CHECK(major.noteToVolts(StepNoteRange::relativeNote(20, 1, -14, 21)) == 3.f);
    const int shiftedMajor = StepNoteRange::octaveNote(2, 1, major.notesPerOctave(), -14, 21);
    CHECK(shiftedMajor == 9);
    CHECK(std::fabs(major.noteToVolts(shiftedMajor) - major.noteToVolts(2) - 1.f) < 0.0001f);

    NoteTrack source, restored, otherTrack;
    source.setName("RANGE");
    CHECK(source.stepNoteMinOctave() == -2 && source.stepNoteMaxOctave() == 3);
    source.setStepNoteMinOctave(-4);
    source.setStepNoteMaxOctave(5);
    source.sequence(0).step(3).setNote(25);
    source.sequence(0).step(3).setGate(true);
    CHECK(otherTrack.stepNoteMinOctave() == -2 && otherTrack.stepNoteMaxOctave() == 3);

    const uint32_t sentinel = 0x1234abcd;
    for (uint32_t version : {uint32_t(ProjectVersion::Version41), uint32_t(ProjectVersion::Latest)}) {
        std::vector<uint8_t> bytes;
        VersionedSerializedWriter writer([&] (const void *data, size_t len) {
            const auto *p = static_cast<const uint8_t *>(data);
            bytes.insert(bytes.end(), p, p + len);
        }, version);
        source.write(writer);
        writer.write(sentinel);
        writer.writeHash();
        size_t offset = 0;
        bool inBounds = true;
        VersionedSerializedReader reader([&] (void *data, size_t len) {
            if (offset + len > bytes.size()) { inBounds = false; std::memset(data, 0, len); return; }
            std::memcpy(data, bytes.data() + offset, len);
            offset += len;
        }, ProjectVersion::Latest);
        restored.read(reader);
        uint32_t readSentinel = 0;
        reader.read(readSentinel);
        CHECK(reader.checkHash() && inBounds && offset == bytes.size());
        CHECK(readSentinel == sentinel);
        CHECK(restored.sequence(0).step(3).gate() && restored.sequence(0).step(3).note() == 25);
        CHECK(restored.stepNoteMinOctave() == (version == 41 ? -2 : -4));
        CHECK(restored.stepNoteMaxOctave() == (version == 41 ? 3 : 5));
    }
    source.setStepNoteMinOctave(20);
    CHECK(source.stepNoteMinOctave() == 5);
    source.setStepNoteMaxOctave(-20);
    CHECK(source.stepNoteMaxOctave() == 5);
    source.clear();
    CHECK(source.stepNoteMinOctave() == -2 && source.stepNoteMaxOctave() == 3);

    for (uint32_t version : {uint32_t(9), uint32_t(Settings::Version)}) {
        UserSettings settings, loaded;
        Calibration calibration, loadedCalibration;
        calibration.clear();
        calibration.cvOutput(0).items()[4] = 0x8000 | 19000;
        settings.get<StepEncoderModeSetting>(SettingStepEncoderMode)->setValue(0);
        settings.get<MenuWrapSetting>(SettingMenuWrap)->setValue(0);
        std::vector<uint8_t> bytes;
        VersionedSerializedWriter writer([&] (const void *data, size_t len) {
            const auto *p = static_cast<const uint8_t *>(data);
            bytes.insert(bytes.end(), p, p + len);
        }, version);
        calibration.write(writer);
        settings.write(writer);
        writer.write(sentinel);
        writer.writeHash();
        size_t offset = 0;
        bool inBounds = true;
        VersionedSerializedReader reader([&] (void *data, size_t len) {
            if (offset + len > bytes.size()) { inBounds = false; std::memset(data, 0, len); return; }
            std::memcpy(data, bytes.data() + offset, len);
            offset += len;
        }, Settings::Version);
        loadedCalibration.read(reader);
        loaded.read(reader);
        uint32_t readSentinel = 0;
        reader.read(readSentinel);
        CHECK(reader.checkHash() && inBounds && offset == bytes.size() && readSentinel == sentinel);
        CHECK(loaded.get<MenuWrapSetting>(SettingMenuWrap)->getValue() == 0);
        CHECK(loadedCalibration.cvOutput(0).items() == calibration.cvOutput(0).items());
        CHECK(loaded.get<StepEncoderModeSetting>(SettingStepEncoderMode)->getValue() == (version == 9 ? 1 : 0));
    }
    std::puts("Step editor range and settings regression checks passed");
    return 0;
}
