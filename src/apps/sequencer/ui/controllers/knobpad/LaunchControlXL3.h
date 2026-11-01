#pragma once

#include <cstdint>

// Novation Launch Control XL 3 Programmer's Reference, DAW mode.
// The custom MIDI port is cable 0; the DAW port is cable 1.
namespace LaunchControlXL3 {

constexpr uint8_t DawCable = 1;
constexpr uint8_t GateColour = 21; // green
constexpr uint8_t PlayheadColour = 5; // red
constexpr uint8_t RecordControl = 118;
constexpr uint8_t PlayControl = 116;

inline bool isTransport(uint8_t cable, uint8_t status, uint8_t number) {
    return cable == DawCable && status == 0xb0 && (number == RecordControl || number == PlayControl);
}

inline bool pressEdge(bool &down, uint8_t value) {
    const bool pressed = value != 0;
    const bool edge = pressed && !down;
    down = pressed;
    return edge;
}

inline uint8_t padControl(int slot) { return uint8_t(37 + slot); }
inline uint8_t sectionControl(bool prev) { return prev ? 103 : 102; }
inline uint8_t encoderModeControl(int row) { return row == 0 ? 69 : 72; }
inline bool isOctaveModifier(uint8_t cable, uint8_t status, uint8_t number) {
    // Solo/Arm is a normal DAW button; Shift suppresses encoder changes for preview.
    return cable == DawCable && status == 0xb0 && number == 65;
}

inline int legacyPadNote(int slot) {
    return 41 + (slot / 4) * 16 + slot % 4;
}

struct Input {
    bool valid;
    uint8_t status;
    uint8_t number;
    uint8_t value;
    bool relative;
};

// Normalize native DAW controls to the existing channel-9 step-editor map.
// Keep accepting the user's matching Custom Mode on the main MIDI port.
inline Input decode(uint8_t cable, uint8_t status, uint8_t number, uint8_t value, bool relative = false) {
    if (cable == 0 && (status == 0x98 || status == 0x88 || status == 0xb8)) {
        return { true, status, number, value, false };
    }
    if (cable != DawCable) {
        return {};
    }
    const int knobNumber = int(number) - (relative ? 64 : 0);
    if (status == 0xbf && knobNumber >= 13 && knobNumber <= 28) {
        // Row 2's native CCs 21..28 become legacy logical slots 29..36.
        const int logicalNumber = knobNumber < 21 ? knobNumber : knobNumber + 8;
        return { true, 0xb8, uint8_t(logicalNumber), value, relative };
    }
    if (status == 0xb0 && number >= 37 && number <= 52) {
        return { true, 0x98, uint8_t(legacyPadNote(number - 37)), value, false };
    }
    if (status == 0xb0 && (number == 103 || number == 102)) {
        return { true, 0xb8, uint8_t(number == 103 ? 106 : 107), value, false };
    }
    return {};
}

inline int relativeDelta(uint8_t value) { return int(value) - 64; }

} // namespace LaunchControlXL3
