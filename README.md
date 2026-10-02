# beamng-revlimit-haptics

Xbox controller haptic feedback for BeamNG.drive, version **1.0.24.0**.
The Lua mod sends vehicle telemetry to an x64 XInput proxy, which controls the
body motors and impulse triggers through Windows.Gaming.Input.

## Download

[Download the ready-to-install ZIP](https://github.com/deliciousTic-Tac/beamng-revlimit-haptics/releases/download/v1.0.24.0/beamng-revlimit-haptics-1.0.24.0.zip)
from the [v1.0.24.0 release](https://github.com/deliciousTic-Tac/beamng-revlimit-haptics/releases/tag/v1.0.24.0).
The repository and release are private; sign in with an account that has access.

The archive includes the compiled `XInput1_4.dll`, the matching Lua mod ZIP,
installation instructions and SHA-256 checksums. This is an **unsigned
prerelease**; compatibility must still be checked in game on your installation.

## Requirements

- Windows 10/11 x64 and BeamNG.drive x64.
- A controller compatible with Windows.Gaming.Input (WGI).
- Trigger motors on the controller to receive impulse-trigger feedback.

## Installation

1. Close BeamNG.drive and extract the downloaded release archive.
2. Back up any existing `XInput1_4.dll` in the game's `Bin64` folder.
3. Copy `Bin64/XInput1_4.dll` from the archive into the game's `Bin64` folder,
   next to `BeamNG.drive.x64.exe`.
4. Copy `mods/beamng-revlimit-haptics-1.0.24.0-mod.zip` into
   `BeamNG\current\mods` in your active BeamNG user folder. Keep this mod ZIP
   compressed. Use the BeamNG launcher to locate the active user folder.
5. Disable older versions of the mod, enable this version and enable
   **Other protocols** in BeamNG's options if necessary.
6. Load a vehicle. Press **F6** to open the haptic settings; the shortcut is
   configurable in **Controls**.

The game installation folder and the active user folder are normally separate.
Use the DLL and Lua mod ZIP from the same release together.

## Effects and settings

The in-game panel controls rev-limiter feedback on RT, ABS and wheel-lock
feedback on LT, gear-shift impacts, and startup vibration. Gear-shift feedback
can use the body motors, triggers or both. Settings are saved by BeamNG and
take effect within approximately 0.1 seconds of simulation time.

See the [controller guide](xbox_proxy/README.md) for detailed behavior,
diagnostics, known limitations and rollback instructions.

## Network and local files

BCH1 sends 96-byte UDP packets to `127.0.0.1:26780` at up to 60 Hz.
The proxy listens only on loopback.

The proxy log is stored at
`%LOCALAPPDATA%\BeamNG-Controller-Haptics\proxy.log`.
Local settings, logs, dumps, caches, installed dependencies, credentials and
signing material are excluded from Git.

Source code is in `xbox_proxy/` and `revlimiter_haptics/mod/`.
The [repository audit](AUDIT.md) documents source selection, sensitive-data
checks and binary provenance.
