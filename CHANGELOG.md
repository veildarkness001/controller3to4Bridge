# Changelog

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
