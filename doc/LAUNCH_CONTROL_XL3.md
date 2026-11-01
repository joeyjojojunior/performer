# Launch Control XL 3: 16-step editing

This implementation targets a Launch Control XL 3 connected directly to the
PERFORMER USB host. It uses Novation's DAW protocol to address the 16 button LEDs;
the older LCXL Factory #1 LED messages are not the Mk3 DAW protocol.

The Mk3 is recognized by USB VID/PID `1235:0148`. Enter a Note track's Steps
editor. PERFORMER enables the DAW connection and selects **DAW Mixer** on the
controller. A saved Custom Mode is not edited or uploaded.

- Press Track left and right together to arm/disarm 16-step editing.
- Top encoder row: steps 1–8; bottom encoder row: steps 9–16. The middle encoder
  row and faders have no step-editor assignment.
- Upper button row: gates 1–8; lower button row: gates 9–16.
- Tap Track left/right separately to change the visible 16-step bank.
- Gate on: green; gate off: unlit; current playback step: red.
- LEDs follow the selected Note track, pattern, visible bank, and gate edits
  made on either the module or controller. Feedback clears when the editor is
  inactive/disarmed.

The existing channel-9 Custom Mode input map is still accepted on USB cable 0.
For remote gate LEDs, keep the controller on DAW Mixer or DAW Control; Custom
Mode surface colours are not the target of this implementation. Returning from
a Custom Mode to a DAW surface invalidates the LED cache and redraws it.

## Hardware checks

This change requires verification on a physical Mk3 after building/flashing:

1. Reconnect the Mk3, select a Note track, open Steps, and arm with Track left +
   right. Confirm DAW Mixer is selected and all 16 gate states are displayed.
2. Toggle each controller button. Verify only its corresponding gate toggles,
   once per press, and releasing does not toggle it again.
3. Edit gates on the module, switch Note tracks and patterns, and visit all four
   banks. Verify the controller always shows the selected bank's gates.
4. Start playback. Confirm the red playhead moves and restores the gate colour
   behind it. Stop playback and check the display against the module.
5. Disarm or leave Steps. Check LEDs clear and the previous loop range returns.
6. Switch to a Custom Mode and back to DAW Mixer. Confirm a complete LED redraw.
7. Turn the top/bottom encoder rows and check the original note range and
   note-layer restriction. Confirm middle row and faders do not edit steps.

Protocol reference:
https://userguides.novationmusic.com/hc/en-gb/articles/27840466544402-Launch-Control-XL-3-programmer-s-DAW-mode

USB descriptor capture:
https://github.com/YuuichiAkagawa/USBH_MIDI/wiki/Novation:LaunchControl-XL-3
