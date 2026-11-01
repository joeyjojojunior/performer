#include "KnobPad16Controller.h"
#include "LaunchControlXL3.h"
#include "StepNoteRange.h"

#include "ui/ControllerManager.h"

#include "core/math/Math.h"

#include <algorithm>

KnobPad16Controller::KnobPad16Controller(ControllerManager &manager, Model &model, Engine &engine, Profile profile) :
    Controller(manager, model, engine),
    _profile(profile),
    _profileMapping(buildProfileMapping(profile))
{
    _padLedCache.fill(-1);
    _model.setKnobPad16Armed(false);
}

KnobPad16Controller::~KnobPad16Controller() {
    clearFeedback();
    if (_modeActive) {
        exitMode();
    }
    _model.setKnobPad16Armed(false);
    if (_profile == Profile::LaunchControlXL3 && _mk3SetupStep > 0) {
        sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeControlChange(0, LaunchControlXL3::RecordControl, 0));
        sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeControlChange(0, LaunchControlXL3::PlayControl, 0));
        sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeNoteOn(15, 12, 0));
    }
}

void KnobPad16Controller::update() {
    if (_profile == Profile::LaunchControlXL3 && _mk3SetupStep < 2) {
        // Enable the DAW protocol, then explicitly select its surface. The Mk3's
        // "DAW Connection: Connect only" setting otherwise leaves Custom Mode up.
        const auto message = _mk3SetupStep == 0
            ? MidiMessage::makeNoteOn(15, 12, 127)
            : MidiMessage::makeControlChange(6, 30, 1);
        if (sendMidi(LaunchControlXL3::DawCable, message)) {
            ++_mk3SetupStep;
            _mk3DawSurface = _mk3SetupStep == 2;
        }
        if (_mk3SetupStep < 2) {
            return;
        }
    }
    if (_profile == Profile::LaunchControlXL3 && _mk3SetupStep == 2 && _mk3DawSurface) {
        // The octave modifier temporarily requires relative reports even in Absolute mode.
        const int mode = _mk3OctavePressed || _model.settings().userSettings().get<StepEncoderModeSetting>(SettingStepEncoderMode)->getValue();
        if (_mk3EncoderModeCache != mode || _mk3EncoderModeTarget >= 0) {
            if (_mk3EncoderModeTarget != mode) {
                _mk3EncoderModeTarget = mode;
                _mk3EncoderModeRow = 0;
            }
            // Configure the top and middle rows independently. Retry a full
            // MIDI queue without assuming either row received the command.
            const uint8_t cc = LaunchControlXL3::encoderModeControl(_mk3EncoderModeRow);
            if (sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeControlChange(6, cc, mode ? 127 : 0))) {
                if (++_mk3EncoderModeRow == 2) {
                    _mk3EncoderModeCache = mode;
                    _mk3EncoderModeTarget = -1;
                }
            }
            return;
        }
    }
    refreshMode();
    syncTransportFeedback();
    syncFeedback();
}

void KnobPad16Controller::uiPageChanged() {
    refreshMode();
}

void KnobPad16Controller::recvMidi(uint8_t cable, const MidiMessage &incoming) {
    MidiMessage message = incoming;
    bool relative = false;
    if (_profile == Profile::LaunchControlXL3) {
        if (LaunchControlXL3::isTransport(cable, incoming.status(), incoming.data0())) {
            const bool record = incoming.controlNumber() == LaunchControlXL3::RecordControl;
            bool &down = record ? _mk3RecordPressed : _mk3PlayPressed;
            if (LaunchControlXL3::pressEdge(down, incoming.controlValue())) {
                refreshMode();
                if (record) {
                    InputAction action;
                    action.kind = InputActionKind::ToggleArmed;
                    applyInputAction(action);
                } else {
                    // Match the module's unshifted Play button: stop/reset
                    // when running, start from the beginning when stopped.
                    _engine.togglePlay();
                }
                syncTransportFeedback();
            }
            return;
        }
        if (LaunchControlXL3::isOctaveModifier(cable, incoming.status(), incoming.data0())) {
            _mk3OctavePressed = incoming.controlValue() != 0;
            return;
        }
        if (cable == LaunchControlXL3::DawCable && incoming.status() == 0xb6 && incoming.controlNumber() == 30) {
            _mk3DawSurface = incoming.controlValue() == 1 || incoming.controlValue() == 2;
            _mk3OctavePressed = false;
            _mk3RecordPressed = _mk3PlayPressed = false;
            _mk3RecordLedCache = _mk3PlayLedCache = -1;
            _padLedCache.fill(-1);
            _prevSectionLedCache = _nextSectionLedCache = -1;
            _mk3EncoderModeCache = _mk3EncoderModeTarget = -1;
            resetComboState();
            return;
        }
        if (cable == LaunchControlXL3::DawCable && incoming.status() == 0xbf) {
            const int desiredMode = _mk3OctavePressed || _model.settings().userSettings().get<StepEncoderModeSetting>(SettingStepEncoderMode)->getValue();
            if (_mk3EncoderModeCache != desiredMode || _mk3EncoderModeTarget >= 0) {
                return; // no knob edits while the two rows are changing modes
            }
        }
        const auto input = LaunchControlXL3::decode(cable, incoming.status(), incoming.data0(), incoming.data1(), _mk3EncoderModeCache == 1);
        if (!input.valid) {
            return;
        }
        message = MidiMessage(input.status, input.number, input.value);
        relative = input.relative;
    }
    refreshMode();

    if (!shouldEnableMode()) {
        return;
    }

    InputAction action;
    if (decodeControlAction(message, action, relative) || decodePadAction(message, action)) {
        applyInputAction(action);
    }
}

bool KnobPad16Controller::shouldEnableMode() const {
    if (_manager.uiPageKind() != ControllerManager::UiPageKind::NoteSequenceEdit) {
        return false;
    }

    if (_model.project().selectedTrack().trackMode() != Track::TrackMode::Note) {
        return false;
    }

    // Keep controller mode active on NOTE tracks even when switching layer:
    // pads must still toggle gates in step view, while knobs remain NOTE-layer only.
    return true;
}

void KnobPad16Controller::refreshMode() {
    const bool enable = shouldEnableMode();
    const int selectedTrack = _model.project().selectedTrackIndex();

    if (!enable) {
        if (_modeActive) {
            exitMode();
        }
        resetComboState();
        return;
    }

    if (!_armed) {
        if (_modeActive) {
            exitMode();
        }
        return;
    }

    if (!_modeActive) {
        enterModeForTrack(selectedTrack);
        return;
    }

    if (_activeTrackIndex != selectedTrack) {
        enterModeForTrack(selectedTrack);
    }
}

bool KnobPad16Controller::isInControllerChannel(const MidiMessage &message) const {
    return _profileMapping.channel < 0 || message.channel() == uint8_t(_profileMapping.channel);
}

bool KnobPad16Controller::decodeControlAction(const MidiMessage &message, InputAction &action, bool relative) {
    if (!message.isControlChange() || !isInControllerChannel(message)) {
        return false;
    }

    const uint8_t cc = message.controlNumber();
    const uint8_t value = message.controlValue();

    const bool isPrevCc = _profileMapping.prevSectionCc >= 0 && cc == uint8_t(_profileMapping.prevSectionCc);
    const bool isNextCc = _profileMapping.nextSectionCc >= 0 && cc == uint8_t(_profileMapping.nextSectionCc);

    if (isPrevCc || isNextCc) {
        const bool pressed = value > 0;

        bool &pressedState = isPrevCc ? _prevPressed : _nextPressed;
        bool &tapCandidate = isPrevCc ? _prevTapCandidate : _nextTapCandidate;

        if (pressed == pressedState) {
            return false;
        }

        pressedState = pressed;

        if (_armed && supportsPadFunctionFeedback()) {
            // Keep these lit while armed, but blink OFF while physically pressed.
            sendSectionLed(isPrevCc, !pressed, true);
        }

        if (pressed) {
            tapCandidate = true;
            if (_prevPressed && _nextPressed && !_comboTriggered) {
                _comboTriggered = true;
                if (_profile == Profile::LaunchControlXL3) {
                    return false; // Mk3 arming belongs exclusively to Record.
                }
                action.kind = InputActionKind::ToggleArmed;
                return true;
            }
            return false;
        }

        const bool shouldMoveSection = tapCandidate && !_comboTriggered;
        tapCandidate = false;

        if (!_prevPressed && !_nextPressed) {
            _comboTriggered = false;
        }

        if (shouldMoveSection && _modeActive) {
            action.kind = InputActionKind::MoveSection;
            action.value = isPrevCc ? -1 : 1;
            return true;
        }

        return false;
    }

    const int slot = findKnobSlot(cc);
    if (slot < 0 || slot >= 16) {
        return false;
    }

    action.slot = slot;

    if (relative) {
        action.kind = InputActionKind::AdjustNoteRelative;
        action.value = LaunchControlXL3::relativeDelta(value);
        return action.value != 0;
    }

    if (_profileMapping.knobMode == KnobMode::Absolute) {
        action.kind = InputActionKind::SetNoteAbsolute;
        action.value = value;
        return true;
    }

    const int delta = decodeRelativeDelta(value);
    if (delta == 0) {
        return false;
    }

    action.kind = InputActionKind::AdjustNoteRelative;
    action.value = delta;
    return true;
}

bool KnobPad16Controller::decodePadAction(const MidiMessage &message, InputAction &action) {
    if (!message.isNoteOn() || message.velocity() == 0 || !isInControllerChannel(message)) {
        return false;
    }

    const int slot = findPadSlot(message.note());
    if (slot < 0 || slot >= 16) {
        return false;
    }

    action.kind = InputActionKind::ToggleGate;
    action.slot = slot;
    return true;
}

void KnobPad16Controller::applyInputAction(const InputAction &action) {
    switch (action.kind) {
    case InputActionKind::ToggleArmed:
        _armed = !_armed;
        _model.setKnobPad16Armed(_armed);
        if (!_armed) {
            if (_modeActive) {
                exitMode();
            }
            clearFeedback();
            return;
        }
        if (shouldEnableMode()) {
            const int selectedTrack = _model.project().selectedTrackIndex();
            if (!_modeActive) {
                enterModeForTrack(selectedTrack);
            } else if (_activeTrackIndex != selectedTrack) {
                enterModeForTrack(selectedTrack);
            }
        }
        syncFeedback(true);
        return;
    case InputActionKind::MoveSection:
        if (_modeActive) {
            moveSection(action.value < 0 ? -1 : 1);
            syncFeedback(true);
        }
        return;
    case InputActionKind::SetNoteAbsolute:
        if (_modeActive && _model.project().selectedNoteSequenceLayer() == NoteSequence::Layer::Note) {
            applyKnobAbsolute(action.slot, uint8_t(clamp(action.value, 0, 127)));
            syncFeedback();
        }
        return;
    case InputActionKind::AdjustNoteRelative:
        if (_modeActive && _model.project().selectedNoteSequenceLayer() == NoteSequence::Layer::Note) {
            applyKnobRelative(action.slot, action.value);
            syncFeedback();
        }
        return;
    case InputActionKind::ToggleGate:
        if (_modeActive) {
            applyPadToggle(action.slot);
            syncFeedback();
        }
        return;
    case InputActionKind::None:
        return;
    }
}

void KnobPad16Controller::resetComboState() {
    _prevPressed = false;
    _nextPressed = false;
    _prevTapCandidate = false;
    _nextTapCandidate = false;
    _comboTriggered = false;
}

void KnobPad16Controller::enterModeForTrack(int trackIndex) {
    _modeActive = true;
    _activeTrackIndex = trackIndex;

    ensureTrackSnapshot(trackIndex);
    applyVisibleWindowLoop();
    syncFeedback(true);
}

void KnobPad16Controller::exitMode() {
    restoreTrackLoopSnapshots();
    _modeActive = false;
    _activeTrackIndex = -1;
    clearFeedback();
}

void KnobPad16Controller::restoreTrackLoopSnapshots() {
    const int selectedPattern = _model.project().selectedPatternIndex();

    for (int trackIndex = 0; trackIndex < CONFIG_TRACK_COUNT; ++trackIndex) {
        auto &snapshot = _trackLoopSnapshots[trackIndex];
        if (!snapshot.valid) {
            continue;
        }

        auto &track = _model.project().track(trackIndex);
        if (track.trackMode() != Track::TrackMode::Note) {
            snapshot.valid = false;
            continue;
        }

        auto &sequence = track.noteTrack().sequence(selectedPattern);
        sequence.setLastStep(snapshot.lastStep);
        sequence.setFirstStep(snapshot.firstStep);

        snapshot.valid = false;
    }
}

void KnobPad16Controller::ensureTrackSnapshot(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= CONFIG_TRACK_COUNT) {
        return;
    }

    auto &snapshot = _trackLoopSnapshots[trackIndex];
    if (snapshot.valid) {
        return;
    }

    auto &track = _model.project().track(trackIndex);
    if (track.trackMode() != Track::TrackMode::Note) {
        return;
    }

    auto &sequence = _model.project().selectedNoteSequence();
    snapshot.valid = true;
    snapshot.firstStep = sequence.firstStep();
    snapshot.lastStep = sequence.lastStep();
}

void KnobPad16Controller::applyVisibleWindowLoop() {
    auto &sequence = _model.project().selectedNoteSequence();
    const int windowFirst = clamp(sequence.section() * 16, 0, CONFIG_STEP_COUNT - 1);
    const int windowLast = std::min(windowFirst + 15, CONFIG_STEP_COUNT - 1);

    sequence.setLastStep(windowLast);
    sequence.setFirstStep(windowFirst);
}

void KnobPad16Controller::moveSection(int direction) {
    auto &sequence = _model.project().selectedNoteSequence();
    constexpr int sectionCount = CONFIG_STEP_COUNT / 16;

    int section = sequence.section();
    section = (section + sectionCount + direction) % sectionCount;
    sequence.setSecion(section);
    applyVisibleWindowLoop();
}

bool KnobPad16Controller::supportsPadFunctionFeedback() const {
    switch (_profile) {
    case Profile::LaunchControlXL3:
        return _mk3SetupStep == 2 && _mk3DawSurface;
    case Profile::LaunchControlXL:
    case Profile::BeatStepPro:
        return _profileMapping.channel >= 0;
    default:
        return false;
    }
}

void KnobPad16Controller::syncTransportFeedback(bool force) {
    if (_profile != Profile::LaunchControlXL3 || !supportsPadFunctionFeedback()) {
        return;
    }
    const auto sendLed = [this, force] (uint8_t control, int value, int &cache) {
        if ((force || cache != value) &&
            sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeControlChange(0, control, uint8_t(value)))) {
            cache = value;
        }
    };
    sendLed(LaunchControlXL3::RecordControl, _armed ? 5 : 0, _mk3RecordLedCache);
    sendLed(LaunchControlXL3::PlayControl, _engine.clockRunning() ? 21 : 0, _mk3PlayLedCache);
}

void KnobPad16Controller::syncFeedback(bool force) {
    if (!supportsPadFunctionFeedback()) {
        return;
    }

    if (!_armed || !_modeActive || !shouldEnableMode()) {
        clearFeedback();
        return;
    }

    auto &sequence = _model.project().selectedNoteSequence();
    const int firstStep = sequence.section() * 16;
    const int playheadSlot = visiblePlayheadSlot(sequence);

    for (int slot = 0; slot < 16; ++slot) {
        const int stepIndex = firstStep + slot;
        if (stepIndex < 0 || stepIndex >= CONFIG_STEP_COUNT) {
            sendPadLed(slot, 0, force);
            continue;
        }

        const auto &step = sequence.step(stepIndex);
        const bool isPlayhead = slot == playheadSlot;
        const int value = isPlayhead ? int(_profileMapping.padPlayheadValue) : (step.gate() ? int(_profileMapping.padOnValue) : 0);
        sendPadLed(slot, value, force);
    }

    sendSectionLed(true, !_prevPressed, force);
    sendSectionLed(false, !_nextPressed, force);
}

void KnobPad16Controller::clearFeedback() {
    if (!supportsPadFunctionFeedback()) {
        return;
    }

    for (int slot = 0; slot < 16; ++slot) {
        sendPadLed(slot, 0, false);
    }
    sendSectionLed(true, false, false);
    sendSectionLed(false, false, false);
}

int KnobPad16Controller::visiblePlayheadSlot(const NoteSequence &sequence) const {
    const auto &trackEngine = _engine.selectedTrackEngine().as<NoteTrackEngine>();
    if (!trackEngine.isActiveSequence(sequence)) {
        return -1;
    }

    const int currentStep = trackEngine.currentStep();
    const int firstStep = sequence.section() * 16;
    const int lastStep = firstStep + 15;
    if (currentStep < firstStep || currentStep > lastStep) {
        return -1;
    }

    return currentStep - firstStep;
}

void KnobPad16Controller::sendPadLed(int slot, int value, bool force) {
    if (slot < 0 || slot >= 16 || !supportsPadFunctionFeedback()) {
        return;
    }

    static constexpr uint8_t padNotes[16] = {
        41, 42, 43, 44,
        57, 58, 59, 60,
        73, 74, 75, 76,
        89, 90, 91, 92,
    };
    value = clamp(value, 0, 127);
    if (!force && _padLedCache[slot] == value) {
        return;
    }

    const bool sent = _profile == Profile::LaunchControlXL3
        ? sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeControlChange(0, LaunchControlXL3::padControl(slot), uint8_t(value)))
        : sendMidi(0, MidiMessage::makeNoteOn(uint8_t(_profileMapping.channel), padNotes[slot], uint8_t(value)));
    if (sent) {
        _padLedCache[slot] = value;
    }
}

void KnobPad16Controller::sendSectionLed(bool prev, bool on, bool force) {
    if (!supportsPadFunctionFeedback()) {
        return;
    }

    const int ccValue = prev ? _profileMapping.prevSectionCc : _profileMapping.nextSectionCc;
    if (ccValue < 0) {
        return;
    }
    const uint8_t cc = uint8_t(ccValue);
    const int value = on ? 127 : 0;
    int &cache = prev ? _prevSectionLedCache : _nextSectionLedCache;
    if (!force && cache == value) {
        return;
    }

    const bool sent = _profile == Profile::LaunchControlXL3
        ? sendMidi(LaunchControlXL3::DawCable, MidiMessage::makeControlChange(0, LaunchControlXL3::sectionControl(prev), on ? 3 : 0))
        : sendMidi(0, MidiMessage::makeControlChange(uint8_t(_profileMapping.channel), cc, uint8_t(value)));
    if (sent) {
        cache = value;
    }
}

int KnobPad16Controller::findKnobSlot(uint8_t cc) const {
    switch (_profile) {
    case Profile::LaunchControlXL3:
    case Profile::LaunchControlXL:
    case Profile::BeatStepPro:
        // LCXL-compatible PERFORMERstep16 mapping:
        // CC 13..20 and 29..36 map to step 1..16 encoders.
        if (cc >= 13 && cc <= 20) { return cc - 13; }
        if (cc >= 29 && cc <= 36) { return 8 + (cc - 29); }
        return -1;
    }

    return -1;
}

int KnobPad16Controller::findPadSlot(uint8_t note) const {
    switch (_profile) {
    case Profile::LaunchControlXL3:
    case Profile::LaunchControlXL:
    case Profile::BeatStepPro:
        // LCXL-compatible PERFORMERstep16 mapping:
        // 41..44, 57..60, 73..76, 89..92 -> step slots 0..15
        if (note >= 41 && note <= 44) { return note - 41; }
        if (note >= 57 && note <= 60) { return 4 + (note - 57); }
        if (note >= 73 && note <= 76) { return 8 + (note - 73); }
        if (note >= 89 && note <= 92) { return 12 + (note - 89); }
        return -1;
    }

    return -1;
}

int KnobPad16Controller::decodeRelativeDelta(uint8_t value) const {
    if (value == 64) {
        return 0;
    }
    if (value < 64) {
        return value;
    }
    return int(value) - 128;
}

int KnobPad16Controller::mapAbsoluteToNoteValue(const NoteSequence &sequence, uint8_t value) const {
    const auto &track = _model.project().selectedTrack().noteTrack();
    const int semitone = StepNoteRange::absoluteSemitone(value, track.stepNoteMinOctave(), track.stepNoteMaxOctave());
    const auto &scale = sequence.selectedScale(_model.project().scale());
    const int note = scale.noteFromVolts(semitone * (1.f / 12.f));

    return clampToKnobNoteRange(sequence, note);
}

int KnobPad16Controller::clampToKnobNoteRange(const NoteSequence &sequence, int note) const {
    const auto &scale = sequence.selectedScale(_model.project().scale());

    const auto &track = _model.project().selectedTrack().noteTrack();
    int noteMin = scale.noteFromVolts(float(track.stepNoteMinOctave()));
    int noteMax = scale.noteFromVolts(float(track.stepNoteMaxOctave()));
    if (noteMin > noteMax) {
        std::swap(noteMin, noteMax);
    }

    noteMin = NoteSequence::Note::clamp(noteMin);
    noteMax = NoteSequence::Note::clamp(noteMax);

    return clamp(note, noteMin, noteMax);
}

void KnobPad16Controller::applyKnobAbsolute(int slot, uint8_t value) {
    auto &sequence = _model.project().selectedNoteSequence();
    const int stepIndex = sequence.section() * 16 + slot;
    if (stepIndex < 0 || stepIndex >= CONFIG_STEP_COUNT) {
        return;
    }

    auto &step = sequence.step(stepIndex);
    step.setNote(mapAbsoluteToNoteValue(sequence, value));
}

void KnobPad16Controller::applyKnobRelative(int slot, int delta) {
    auto &sequence = _model.project().selectedNoteSequence();
    const int stepIndex = sequence.section() * 16 + slot;
    if (stepIndex < 0 || stepIndex >= CONFIG_STEP_COUNT) {
        return;
    }

    if (delta == 0) {
        return;
    }

    auto &step = sequence.step(stepIndex);
    const int low = clampToKnobNoteRange(sequence, NoteSequence::Note::Min);
    const int high = clampToKnobNoteRange(sequence, NoteSequence::Note::Max);
    if (_profile == Profile::LaunchControlXL3 && _mk3OctavePressed) {
        const auto &scale = step.bypassScale() ? Scale::get(0) : sequence.selectedScale(_model.project().scale());
        const auto &track = _model.project().selectedTrack().noteTrack();
        const int octaveLow = NoteSequence::Note::clamp(scale.noteFromVolts(float(track.stepNoteMinOctave())));
        const int octaveHigh = NoteSequence::Note::clamp(scale.noteFromVolts(float(track.stepNoteMaxOctave())));
        step.setNote(StepNoteRange::octaveNote(step.note(), delta, scale.notesPerOctave(), octaveLow, octaveHigh));
    } else {
        step.setNote(StepNoteRange::relativeNote(step.note(), delta, low, high));
    }
}

void KnobPad16Controller::applyPadToggle(int slot) {
    auto &sequence = _model.project().selectedNoteSequence();
    const int stepIndex = sequence.section() * 16 + slot;
    if (stepIndex < 0 || stepIndex >= CONFIG_STEP_COUNT) {
        return;
    }

    sequence.step(stepIndex).toggleGate();
}

KnobPad16Controller::ProfileMapping KnobPad16Controller::buildProfileMapping(Profile profile) const {
    ProfileMapping mapping;

    switch (profile) {
    case Profile::LaunchControlXL3:
        mapping.prevSectionCc = 106;
        mapping.nextSectionCc = 107;
        mapping.channel = 8; // normalized native controls and legacy Custom Mode
        mapping.padOnValue = LaunchControlXL3::GateColour;
        mapping.padPlayheadValue = LaunchControlXL3::PlayheadColour;
        return mapping;
    case Profile::LaunchControlXL:
        // LaunchControl XL Factory Preset #1.
        mapping.knobMode = KnobMode::Absolute;
        mapping.prevSectionCc = 106;
        mapping.nextSectionCc = 107;
        mapping.channel = 8; // MIDI channel 9 (0-based index)
        mapping.padOnValue = 63; // amber-like ON
        mapping.padPlayheadValue = 15; // red-like playhead
        return mapping;
    case Profile::BeatStepPro:
        // BSP template aligned to LCXL Factory #1 map:
        // - Absolute knobs
        // - CC 106 / 107 for prev/next section (Ptn1 / Ptn2)
        mapping.knobMode = KnobMode::Absolute;
        mapping.prevSectionCc = 106;
        mapping.nextSectionCc = 107;
        mapping.channel = 8; // keep same logical channel as LCXL template
        mapping.padOnValue = 127; // stronger ON level for BSP pads
        mapping.padPlayheadValue = 15; // fallback distinct level for playhead
        return mapping;
    }

    return mapping;
}
