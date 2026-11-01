#pragma once

#include <cstdint>

// Novation Launch Control XL 3 Programmer's Reference, DAW mode.
// The custom MIDI port is cable 0; the DAW port is cable 1.
namespace LaunchControlXL3 {

constexpr uint8_t DawCable = 1;
constexpr uint8_t GateColour = 21; // green
constexpr uint8_t PlayheadColour = 5; // red

inline uint8_t padControl(int slot) { return uint8_t(37 + slot); }
inline uint8_t sectionControl(bool prev) { return prev ? 103 : 102; }

inline int legacyPadNote(int slot) {
    return 41 + (slot / 4) * 16 + slot % 4;
}

struct Input {
    bool valid;
    uint8_t status;
    uint8_t number;
    uint8_t value;
};

// Normalize native DAW controls to the existing channel-9 step-editor map.
// Keep accepting the user's matching Custom Mode on the main MIDI port.
inline Input decode(uint8_t cable, uint8_t status, uint8_t number, uint8_t value) {
    if (cable == 0 && (status == 0x98 || status == 0x88 || status == 0xb8)) {
        return { true, status, number, value };
    }
    if (cable != DawCable) {
        return {};
    }
    if (status == 0xbf && ((number >= 13 && number <= 20) || (number >= 29 && number <= 36))) {
        return { true, 0xb8, number, value };
    }
    if (status == 0xb0 && number >= 37 && number <= 52) {
        return { true, 0x98, uint8_t(legacyPadNote(number - 37)), value };
    }
    if (status == 0xb0 && (number == 103 || number == 102)) {
        return { true, 0xb8, uint8_t(number == 103 ? 106 : 107), value };
    }
    return {};
}

} // namespace LaunchControlXL3
