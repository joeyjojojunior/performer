#pragma once

#include <algorithm>

namespace StepNoteRange {

inline int absoluteSemitone(int value, int minOctave, int maxOctave) {
    return minOctave * 12 + (value * (maxOctave - minOctave) * 12 + 63) / 127;
}

inline int relativeNote(int note, int delta, int low, int high) {
    // Existing notes outside a newly narrowed range move inward gradually;
    // they must not jump to a boundary on the first encoder tick.
    return std::max(std::min(note, low), std::min(std::max(note, high), note + delta));
}

inline int octaveNote(int note, int delta, int notesPerOctave, int low, int high) {
    if (notesPerOctave <= 0) {
        return note;
    }
    const int direction = delta < 0 ? -1 : 1;
    while (delta != 0) {
        const int next = note + direction * notesPerOctave;
        // Only complete octaves: clamping a partial octave would change the
        // pitch class. Notes outside a narrowed range may still move inward.
        if (next < std::min(note, low) || next > std::max(note, high)) {
            break;
        }
        note = next;
        delta -= direction;
    }
    return note;
}

} // namespace StepNoteRange
