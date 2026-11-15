# Xbox Haptic Feedback Controller

## 1.0.26.0

- Wheel-lock warnings now use longitudinal speed and intact, loaded wheels with
  brake capacity and applied braking torque. Sideways movement no longer acts
  as longitudinal braking speed; reverse movement is supported.
- Added configurable body mixing: maximum, clamped addition, custom priority
  per motor, or legacy directional shifts. Game and mod gains are independent.
- Added local DLL diagnostics: version, pairing, telemetry, output backend,
  last API result and the four last successful output commands.
- Overlay channels now use DLL feedback instead of a second Lua effect model.
  Replies are sampled at up to 30 Hz and cleared when stale or mismatched.
  These are API commands, not physical actuator measurements.
- Added WGI call timing, lifetime peaks, slow-call counts and throttled logs.
  Timers detect slow calls; they cannot interrupt a blocked driver.
- Added native and Lua regressions for mixing, diagnostic UDP round trips,
  malformed/stale replies, wheel filtering and feedback expiry.

## 1.0.25.0

- Removed false startup pulses on initial telemetry, vehicle loading and recovery.
- Prevented RPM-inferred shift pulses from replaying on delayed gear confirmation.
- Deferred Controls-category registration until the input module is available.
- Allowed idle pairing when both APIs expose exactly one matching controller.

## Validation and remaining limits

Unsigned Windows x64 prerelease. Native regression tests, Lua tests and packaging
checks run before publication. Real controller and BeamNG compatibility still
require in-game testing. No external bridge, worker thread or crash-independent
watchdog was added. Output targets XInput controller 0; split-screen/multiseat
is not supported. Use the matching DLL and inner mod ZIP from the same package.
