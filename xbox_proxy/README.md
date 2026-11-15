# Xbox Haptic Feedback Controller - 1.0.26.0

The [ready-to-install release](https://github.com/deliciousTic-Tac/Beamng-Xbox-xinput-Haptic-Feedback-Controller/releases/tag/v1.0.26.0)
contains the compiled DLL and its matching Lua mod ZIP.
Follow the [installation instructions](../README.md#installation).

## Installing this local candidate

1. Close BeamNG before replacing its DLL.
2. Extract the outer archive. Back up any existing `XInput1_4.dll` from the
   game's `Bin64` directory, then copy `Bin64/XInput1_4.dll` there, next to
   `BeamNG.drive.x64.exe`. Never put this DLL in Windows/System32.
3. Copy the ZIP inside `mods/` into the active BeamNG user folder's `mods/`
   directory. Keep that inner ZIP compressed and disable older haptics versions.
4. Enable the mod and **Other protocols** if necessary. Load a combustion-engine
   vehicle and press F6 to open settings. The shortcut is configurable in Controls.
5. Test starting an engine, upshifts/downshifts, ABS, pause and changing vehicles.
   This unsigned candidate passed automated checks; hardware testing is pending.

The user folder and game installation are separate. Use the matching DLL and
mod ZIP from this archive. OutGauge and MotionSim are not required by BCH1.

## Architecture

The Lua mod is in `../revlimiter_haptics/mod`. Its BCH1 vehicle protocol sends
96-byte UDP packets to `127.0.0.1:26780` at up to 60 Hz. The x64 XInput1_4 proxy
uses WinRT to drive the controller's body motors and impulse triggers. Its
synchronous pump runs from XInput calls; no background worker or external
service is installed. `DllMain` remains minimal.

The statically linked CRT (`/MT`) retains thread notifications; the proxy does
not call `DisableThreadLibraryCalls`.

Runtime requirements are Windows 10/11 x64, BeamNG x64 and a WGI-compatible
controller. BeamNG 0.39 compatibility mentioned in the code must be checked
in game. The BCH1 wire format and XInput export ordinals are preserved.
Use the DLL and Lua mod ZIP from the same release.

## Known limitations

- WGI controller selection compares the input of XInput controller 0
  against all WGI gamepads. A unique match must be confirmed by two readings
  at least 100 ms apart. When both APIs expose exactly one controller and its
  readings agree, pairing also works at rest. Otherwise, without distinctive
  input (a button, trigger or stick), or if matching is ambiguous, the DLL retains ordinary XInput vibration
  without sending WGI effects. Real multi-controller matching still needs
  in-game testing.
- The 250 ms telemetry watchdog depends on XInput calls. If those calls stop,
  there is no independent worker to send zero vibration. Behavior after a crash
  or forced shutdown depends on the driver and Windows; process cleanup cannot
  guarantee that the motors stop. A reliable standalone companion would need
  to own WGI output throughout the session. A process started only at shutdown
  cannot reliably identify the same controller.
- `XInputEnable(FALSE)` requests zero vibration, disables WGI output and closes
  the socket. TRUE restores output using the latest ordinary motor request;
  stale telemetry effects are not replayed. Games are not required to call this
  API on every focus change under Windows 10/11.
- A paired controller is cached only if it exposes `IAgileObject`. Access is
  serialized, membership in the WGI list is checked every second, and the cache
  is invalidated on removal, reading or vibration errors, or an input mismatch.
  Other COM references remain local. Changed output is limited to one update
  every 16 ms; device enumeration does not run at that frequency.
- With no paired WGI controller and no requested vibration, the proxy defers
  WinRT initialization and controller enumeration. This avoids blocking
  BeamNG's XInput thread at startup when a controller is absent. The first
  nonzero vibration request starts discovery immediately.

## Settings and diagnostics

Version 1.0.25.0 fixes false startup effects on initial telemetry/recovery,
replayed gear-shift pulses after delayed gear confirmation, and missing Controls
categories when the input module initializes after the mod. A single controller
can now pair at rest, with two matching readings and no second XInput device.
The BCH1 packet format is unchanged. These fixes have automated regression
coverage; BeamNG and physical-controller validation is still required.

**F6** opens **Xbox Haptic Feedback Controller**; the shortcut can be changed in
**Controls**. Settings take effect within approximately 0.1 seconds of
simulation time, plus a tick. The UI writes only values that change.
The four `controllerHapticsVisualBodyLeft/Right` and
`controllerHapticsVisualTriggerLeft/Right` electrics values now come from the
last successful output API command, sampled at up to 30 Hz. Lua no longer
computes a second approximation. They are not physical motor measurements.
`controllerHapticsOutputValid` is 1 only for fresh feedback on the current
vehicle with live telemetry. Feedback is cleared on stale/missing responses,
vehicle changes and extension unload; short pulses may be missed by sampling.

Logs are stored at `%LOCALAPPDATA%\BeamNG-Controller-Haptics\proxy.log`,
with timestamps and severity levels. The file restarts when it reaches 1 MiB.
If the local application-data path is missing or too long, logging is skipped.
UDP errors include the WinSock error code, with retries every five seconds.
The GE extension `controllerHapticsDiagnostics` sends small `BCQ1` requests
on the same loopback port, from an ephemeral loopback-bound UDP socket. `BCD1`
JSON replies report DLL version, XInput-0/WGI pairing, telemetry, enabled state,
last output result, backend and four last-command values. The socket is opened
after mod mounting; all loads in modScript remain flat. Replies are validated,
ordered by request sequence, and expire after 0.5 s of real update time. A
`ControllerHapticsStatus` GUI event is also available for HUD integrations.
These messages provide local diagnostics, not an authenticated IPC channel.
An unavailable listener cannot distinguish a missing DLL from stopped XInput
polling, a disabled proxy or a port conflict.

QPC timers measure initialization, device scans, vibration calls and apartment
teardown. The panel reports last/peak times and counts calls >=5 ms; the log
records slow-call summaries at most once per 10 s. This instruments the
synchronous path; it does not interrupt blocked drivers or add a watchdog.
The measurements are lifetime peaks/counters for the loaded DLL.

The body mixer offers maximum (default), clamped addition, per-motor custom
priority and legacy directional shifts. Game body gain and mod gain (body plus
triggers) are independent 0-100% settings. Only legacy directional mode suppresses
the opposite native motor during shifts. Mixing requires WGI pairing; ordinary
XInput fallback preserves the game's original body requests. Mixer settings
are refreshed by the diagnostics query and revert to maximum/100%/100% after
one second without control messages. No thread or external bridge was added.

The lock heuristic now uses absolute longitudinal speed, loaded intact wheels
with brake capacity and current braking torque, and rolling speed from
angularVelocityBrakeCouple (falling back to angularVelocity). It works in
reverse and avoids comparing wheel speed against sideways vehicle motion.
It remains a slip heuristic, not a tire contact-patch model. ABS pulse detection
still uses the game's ABS flag independently.

If telemetry is absent, check that the mod is enabled, a vehicle is loaded and
custom protocols are enabled. An occupied UDP port prevents the proxy from
receiving telemetry; another bridge or telemetry listener may be using it.

## Rollback

Close BeamNG, remove this release's mod ZIP from the active user folder and
restore the exact DLL backup made before installation. If there was no local
`XInput1_4.dll` before installation, remove the DLL supplied by this release.

A Steam file verification can restore official game files, but it does not
replace a backup of a previous proxy DLL.

## References

- Microsoft: [XInputEnable](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputenable).
- Microsoft: [Gamepad and vibration](https://learn.microsoft.com/en-us/windows/uwp/gaming/gamepad-and-vibration).
- Microsoft: [DisableThreadLibraryCalls and the static CRT](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-disablethreadlibrarycalls).
- [Repository audit](../AUDIT.md): selected sources, exclusions and sensitive-data checks.
