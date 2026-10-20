# Changelog

## [1.3.0] - 2026-10-20

- Unify USB and internal Bluetooth support in one release ELF, with automatic USB priority and a fresh controller session when changing transport.
- Include the transport switching and cleanup fixes developed in the 1.2.x candidates below. Bluetooth still requires an already paired DS3.
- Simplify installation instructions to download the desired version from Releases.

The bridge implementation was checked on PS4 in candidate 1.2.3: USB → Bluetooth → USB → Bluetooth, working game menus, brief wireless gameplay, and verified clean shutdown. The 1.3.0 build updates the version without changing that implementation; it has not been separately tested on console. Changing transport may require user selection again; longer sessions, rest mode, and compatibility with other games remain unverified.

## [1.2.3] - Unreleased

- Select USB before starting a Bluetooth backend. Wired sessions no longer retain a Bluetooth listener, registration, or temporary Bluetooth privilege/profile state.
- If USB arrives during the initial Bluetooth wait, unwind that activation before creating a wired virtual controller.
- Add a guarded read-only known-controller link diagnostic. It reports connection state and channel flags without controller identifiers, registration, or pairing writes.

Checked on PS4 firmware 13.52 with GoldHEN v2.4b18.12 and an original Sony DS3. One ELF load completed USB → Bluetooth → USB → Bluetooth, with user selection and working game menus after each change, followed by brief wireless gameplay. The 563-second test ended with verified virtual-controller removal, Bluetooth link absence, and privilege/profile restoration. Local validation: 40 C test groups with ASan/UBSan and 10 Python tests.

Changing transport recreates the virtual controller and prompts for user selection. An earlier candidate lost input in an already running application after this recreation, while PS4 menus still worked. Choose the transport before opening a game; compatibility with other games, longer sessions, and rest mode remain unverified.

## [1.2.2] - Unreleased

- Restart the controller session inside the same ELF on transport changes, physical errors, or stale input, sending neutral before closing the virtual controller, Bluetooth listener, and USB backend in order.
- Recreate transport and credential state before accepting a new source. User selection may appear again after a transport change; the ELF does not need to be reloaded.
- Verify USB interface release and library unload, stopping on cleanup failure instead of starting another activation.
- Cover neutral-before-restart, loss during activation, stale streams, repeated full activation lifetimes, and cleanup failures in local tests.

Manual PS4 menu tests confirmed five transport changes, including three USB-to-Bluetooth recoveries, with one ELF load. Each change completed VDA/Bluetooth/USB cleanup before creating the next virtual controller; the user confirmed control in both modes and reassigned their user after each transition. One Bluetooth menu power-off and PS reactivation cycle also completed with restored control and verified cleanup. Longer gameplay sessions and rest mode remain unverified. The preceding candidate completed Bluetooth initialization but failed to receive input after USB.

## [1.2.1] - Unreleased

- Disconnect and verify the known DS3 Bluetooth link before activating USB, discard queued Bluetooth events, and clear the previous stream state while retaining the listener and virtual controller.
- Rearm Bluetooth after leaving USB, requiring a fresh PS activation instead of reusing cached reports. Failed link cleanup stops the bridge.
- Add regression tests for delayed Bluetooth ready/completion events across three cable handoffs and failed physical disconnection.

This candidate addresses a failure observed in 1.2.0: the first USB → Bluetooth → USB cycle worked in PS4 menus, but a second USB removal did not restore input despite a steady controller light. The precise cause was not established. In a manual PS4 test of 1.2.1, initial Bluetooth and USB input worked, but the return to Bluetooth completed F4 initialization without receiving new input, even after PS. The test stopped with verified cleanup; the issue remained unresolved.

## [1.2.0] - Unreleased

- Combined USB and internal Bluetooth in one ELF, with automatic USB priority and one virtual controller across transport changes.
- Clear held controls and touch contacts when changing transport; releasing Select rearms touchpad emulation.
- Wait for valid physical input before opening user selection in either mode. USB works without Bluetooth pairing when Bluetooth initialization rolls back successfully.
- Keep Bluetooth event processing alive during USB use and preserve VDA-before-Bluetooth cleanup. Failed Bluetooth rollback stops the bridge instead of continuing with unknown state.
- Retain Bluetooth menu power-off and PS reactivation. For USB menu power-off, wait for cable removal before arming another activation.
- Add `unified-bridge` build and console-tool targets; preserve the separate USB and Bluetooth targets.

One manual PS4 menu cycle confirmed USB → Bluetooth → USB with the same virtual controller and one ELF load. A second USB removal failed to restore Bluetooth input; the bridge stopped with verified cleanup. Repeated switching is not reliable in this version. Bluetooth still requires prior pairing. Power-off in the combined build, rumble, and rest mode remain unverified or unsupported.

## [1.1.0] - 2026-10-19

- Fixed DS3 LEDs continuing to flash after a successful Bluetooth connection. LED 1 now stays on while connected; it indicates the bridge connection, not the PS4 user slot.
- Fixed native menu power-off: selecting **Sound/Devices → Turn Off Device → DUALSHOCK 4** now turns off the bridged DS3. Pressing PS reconnects it and opens user selection without restarting the console or reloading the payload.

Both fixes were checked in manual PS4 tests; one complete menu power-off and PS reactivation cycle was confirmed. Repeated cycles and coexistence with an active physical DS4 remain untested. The release ELF uses Bluetooth and requires an already paired DS3.

## [1.0.0] - 2026-10-19

First public release.

- DS3 input on PS4 with GoldHEN through USB or internal Bluetooth, presented as a virtual DS4.
- Standard controls, PS, Options, Share, and basic touchpad movement and click emulation.
- Bluetooth loading through GoldHEN LaunchPad and AutoRun, with user selection deferred until the DS3 connects.
- Input-loss handling, USB reconnection, and clean session shutdown.
- Build tools, local tests, and installation instructions.

The release ELF uses Bluetooth and requires an already paired DS3. USB is available as a separate build. Tested on firmware 13.52 with GoldHEN v2.4b18.12; Bluetooth remains experimental. Rumble, player LEDs, and initial pairing are not included. See [README.md](README.md) for compatibility and remaining tests.
