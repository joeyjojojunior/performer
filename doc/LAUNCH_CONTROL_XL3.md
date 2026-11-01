# Launch Control XL 3: 16-step editing

This implementation targets a Launch Control XL 3 connected directly to the
PERFORMER USB host. It uses Novation's DAW protocol to address the 16 button LEDs;
the older LCXL Factory #1 LED messages are not the Mk3 DAW protocol.

The Mk3 is recognized by USB VID/PID `1235:0148`. On connection,
PERFORMER enables the DAW connection and selects **DAW Mixer** on the
controller. A saved Custom Mode is not edited or uploaded.

- Press **Record** to arm/disarm 16-step editing. Its red LED shows the armed
  state. This does not enable MIDI note recording.
- Press **Play** for the same start/stop/reset action as the module's Play button:
  pressing while running stops and resets playback to the beginning; pressing
  while stopped starts from the beginning. Its green
  LED follows playback, including changes from the module. Transport works
  with editing disarmed and on other pages, using the normal clock-mode rules.
- Top encoder row: steps 1–8; middle encoder row: steps 9–16. The third encoder
  row and faders have no step-editor assignment.
- Upper button row: gates 1–8; lower button row: gates 9–16.
- Tap Track left/right separately to change the visible 16-step bank.
  Pressing both together no longer changes the editing mode.
- Gate on: green; gate off: unlit; current playback step: red.
- LEDs follow the selected Note track, pattern, visible bank, and gate edits
  made on either the module or controller. Feedback clears when the editor is
  inactive/disarmed.

The existing channel-9 Custom Mode input map is still accepted on USB cable 0.
For remote gate LEDs, keep the controller on DAW Mixer or DAW Control; Custom
Mode surface colours are not the target of this implementation. Returning from
a Custom Mode to a DAW surface invalidates the LED cache and redraws it.

## Encoder mode and note range

In **System > Settings**, **16Step Encoders** selects `relative` (default) or
`absolute` for the Mk3 DAW encoders. The change takes effect automatically;
save System settings to retain the choice after power-off. In relative mode,
turning an encoder adjusts the current step note in scale degrees instead of
replacing it with an absolute knob position. The Mk3's top and middle encoder
rows are configured by the firmware; no Components edit is needed. The legacy
channel-9 Custom Mode mapping continues to use absolute values.

Each Note track has **16Step Min Oct** and **16Step Max Oct** on its Track page.
They default to **-2** and **+3**, apply across that track's patterns, and are
saved with the project/track. Each limit is adjustable between -5 and +5 octaves;
the minimum cannot exceed the maximum. These limits apply only to external
16-step note entry, before track transposition, and respect the sequence's
selected scale and stored-note limits. Changing them does not rewrite notes.

Absolute mode spans the configured range. Relative mode stops at its limits;
if a pre-existing note is outside them, inward turns move it back gradually
and outward turns leave it unchanged.

Hold **Solo/Arm** while turning a step encoder to transpose that step by whole
octaves. Release Solo/Arm to resume normal note edits. A major-scale octave is
seven scale steps; a chromatic octave is twelve. Transpose preserves pitch class
and allows only complete octaves within the track's limits. Solo/Arm temporarily
requests relative encoder reports even when the global setting is Absolute;
releasing it restores the configured mode.

The Mk3's **Shift** key retains its built-in value preview. Hardware testing
confirmed Shift suppresses encoder changes, so it is not used for transpose.

Saving now uses project/track format **42** and global settings format **10**.
Previous formats remain readable, receiving the defaults above. Older firmware
cannot read newly saved data: retain copies of projects and settings (including
calibration) if you plan to roll back.

## Hardware checks

This change requires verification on a physical Mk3 after building/flashing:

1. Reconnect the Mk3, select a Note track, open Steps, and arm with Record.
   Confirm DAW Mixer is selected, Record is red, and all 16 gate states are displayed.
2. Toggle each controller button. Verify only its corresponding gate toggles,
   once per press, and releasing does not toggle it again.
3. Edit gates on the module, switch Note tracks and patterns, and visit all four
   banks. Verify the controller always shows the selected bank's gates.
4. Start playback. Confirm the red playhead moves and restores the gate colour
   behind it. Stop playback and check the display against the module.
5. Disarm or leave Steps. Check LEDs clear and the previous loop range returns.
6. Switch to a Custom Mode and back to DAW Mixer. Confirm a complete LED redraw.
7. Turn the top/middle encoder rows in relative mode. Switch tracks/banks and
   turn again: each edit must start at that step's current note with no jump.
   Confirm the note-layer restriction and that third row/faders do not edit steps.
8. Change **16Step Encoders** to absolute and back. Check clockwise/counterclockwise
   edits in each mode, then save settings and power-cycle to check persistence.
9. Give two tracks different minimum/maximum octaves. Check both endpoints in
   absolute mode and both limits in relative mode, also with a non-chromatic
   scale. Save/reload the project and confirm each track keeps its range.
10. Hold Solo/Arm and turn an encoder both ways: only its step should move by
    whole octaves. Release and check normal note edits resume. Repeat with
    Absolute selected, a major scale, and notes near the range limits. Shift
    should still preview without changing notes; Mute/Select must not transpose.
11. Press/release Record repeatedly. Each press toggles editing exactly once;
    release must not toggle it. Track left + right must not arm/disarm editing.
12. Press/release Play repeatedly, including with editing disarmed and outside
    Steps. Check a press while running stops/resets playback, a second press
    starts from the beginning, and the green LED
    follows playback. Also change playback from the module to check LED feedback.

Protocol reference:
https://userguides.novationmusic.com/hc/en-gb/articles/27840466544402-Launch-Control-XL-3-programmer-s-DAW-mode

USB descriptor capture:
https://github.com/YuuichiAkagawa/USBH_MIDI/wiki/Novation:LaunchControl-XL-3
