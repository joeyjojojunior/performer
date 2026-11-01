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
        CHECK(decode(1, 0xbf, uint8_t(29 + knob), 64).valid);
        CHECK(!decode(1, 0xbf, uint8_t(21 + knob), 64).valid); // middle row is unused
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

    std::puts("Launch Control XL 3 MIDI regression checks passed");
    return 0;
}
