# Xbox Haptic Feedback Controller - 1.0.24.0

The [ready-to-install release](https://github.com/deliciousTic-Tac/beamng-revlimit-haptics/releases/tag/v1.0.24.0)
contains the compiled DLL and its matching Lua mod ZIP.
Follow the [installation instructions](../README.md#installation).

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

- WGI controller selection compares the active input of XInput controller 0
  against all WGI gamepads. A unique match must be confirmed by two readings
  at least 100 ms apart. Without distinctive input (a button, trigger or stick),
  or if matching is ambiguous, the DLL retains ordinary XInput vibration
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

**F6** opens **Xbox Haptic Feedback Controller**; the shortcut can be changed in
**Controls**. Settings take effect within approximately 0.1 seconds of
simulation time, plus a tick. The UI writes only values that change.
The four published `electrics` values are approximate visual indicators,
not measurements of the physical motors.

Logs are stored at `%LOCALAPPDATA%\BeamNG-Controller-Haptics\proxy.log`,
with timestamps and severity levels. The file restarts when it reaches 1 MiB.
If the local application-data path is missing or too long, logging is skipped.
UDP errors include the WinSock error code, with retries every five seconds.
The DLL does not send diagnostic feedback to the in-game UI.

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
