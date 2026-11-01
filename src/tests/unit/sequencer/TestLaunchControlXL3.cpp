#include "ui/controllers/knobpad/LaunchControlXL3.h"

#include <cstdio>

// Also runnable without the simulator's SDL/OpenGL dependencies.
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main() {
    using namespace LaunchControlXL3;

    for (int slot = 0; slot < 16; ++slot) {
        const auto pressed = decode(1, 0xb0, uint8_t(37 + slot), 127);
        CHECK(pressed.valid && pressed.status == 0x98);
        CHECK(pressed.number == legacyPadNote(slot) && pressed.value == 127);
        const auto released = decode(1, 0xb0, uint8_t(37 + slot), 0);
        CHECK(released.valid && released.value == 0); // never toggle on release
        CHECK(padControl(slot) == 37 + slot);
        const auto custom = decode(0, 0x98, uint8_t(legacyPadNote(slot)), 100);
        CHECK(custom.valid && custom.number == pressed.number);
    }

    for (int knob = 0; knob < 8; ++knob) {
        CHECK(decode(1, 0xbf, uint8_t(13 + knob), 64).valid);
        const auto middle = decode(1, 0xbf, uint8_t(21 + knob), 64);
        CHECK(middle.valid && middle.number == 29 + knob);
        CHECK(!decode(1, 0xbf, uint8_t(29 + knob), 64).valid); // third row is unused
    }

    const auto prev = decode(1, 0xb0, 103, 127);
    const auto next = decode(1, 0xb0, 102, 127);
    CHECK(prev.valid && prev.status == 0xb8 && prev.number == 106);
    CHECK(next.valid && next.status == 0xb8 && next.number == 107);
    CHECK(decode(1, 0xb0, 103, 0).value == 0);
    CHECK(sectionControl(true) == 103 && sectionControl(false) == 102);

    // DAW LEDs/controls and similarly numbered Custom Mode controls must not
    // cross ports or channels. Touch and feature reports are not step edits.
    CHECK(!decode(0, 0xb0, 37, 127).valid);
    CHECK(!decode(1, 0xb8, 13, 127).valid);
    CHECK(!decode(2, 0xb0, 37, 127).valid);
    CHECK(!decode(1, 0xbe, 13, 127).valid);
    CHECK(!decode(1, 0xb6, 30, 1).valid);
    CHECK(!decode(1, 0xb0, 36, 127).valid);
    CHECK(!decode(1, 0xb0, 53, 127).valid);
    CHECK(GateColour != 0 && PlayheadColour != 0 && GateColour != PlayheadColour);

    for (int knob = 0; knob < 8; ++knob) {
        const auto top = decode(1, 0xbf, uint8_t(77 + knob), 65, true);
        const auto middle = decode(1, 0xbf, uint8_t(85 + knob), 63, true);
        CHECK(top.valid && top.relative && top.number == 13 + knob);
        CHECK(middle.valid && middle.relative && middle.number == 29 + knob);
        CHECK(relativeDelta(top.value) == 1 && relativeDelta(middle.value) == -1);
        CHECK(!decode(1, 0xbf, uint8_t(13 + knob), 65, true).valid);
        CHECK(!decode(1, 0xbf, uint8_t(77 + knob), 65, false).valid);
        CHECK(!decode(1, 0xbf, uint8_t(93 + knob), 65, true).valid);
    }
    CHECK(relativeDelta(64) == 0);
    CHECK(relativeDelta(70) == 6 && relativeDelta(58) == -6);
    CHECK(decode(1, 0xb0, 37, 127, true).valid); // pads unchanged
    CHECK(decode(1, 0xb0, 103, 127, true).number == 106); // navigation unchanged
    CHECK(!decode(0, 0xb8, 13, 65, true).relative); // Custom Mode stays absolute
    CHECK(encoderModeControl(0) == 69 && encoderModeControl(1) == 72);
    CHECK(isOctaveModifier(1, 0xb0, 65));
    CHECK(!isOctaveModifier(0, 0xb0, 65));
    CHECK(!isOctaveModifier(1, 0xb6, 65));
    CHECK(!isOctaveModifier(1, 0xb6, 63)); // physical Shift stays preview-only
    CHECK(!isOctaveModifier(1, 0xb0, 63));
    CHECK(!isOctaveModifier(1, 0xb0, 66)); // Mute/Select is not the modifier
    CHECK(!decode(1, 0xb0, 65, 127).valid); // modifier never becomes a step edit
    CHECK(RecordControl == 118 && PlayControl == 116);
    CHECK(isTransport(1, 0xb0, 118) && isTransport(1, 0xb0, 116));
    CHECK(!isTransport(0, 0xb0, 118) && !isTransport(1, 0xb8, 118));
    CHECK(!isTransport(1, 0xb0, 103) && !isTransport(1, 0xb6, 63));
    CHECK(!decode(1, 0xb0, 118, 127).valid); // transport never becomes a step edit
    CHECK(!decode(1, 0xb0, 116, 127).valid);
    bool recordDown = false, playDown = false;
    CHECK(pressEdge(recordDown, 127));
    CHECK(!pressEdge(recordDown, 127)); // duplicate reports must not toggle twice
    CHECK(pressEdge(playDown, 127)); // independent button state
    CHECK(!pressEdge(recordDown, 0));
    CHECK(!pressEdge(recordDown, 0));
    CHECK(pressEdge(recordDown, 127));
    CHECK(!pressEdge(playDown, 0));
    CHECK(pressEdge(playDown, 127));

    std::puts("Launch Control XL 3 MIDI regression checks passed");
    return 0;
}
