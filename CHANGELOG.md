# Changelog

## [1.0.0] - 2026-10-19

First public release.

- DS3 input on PS4 with GoldHEN through USB or internal Bluetooth, presented as a virtual DS4.
- Standard controls, PS, Options, Share, and basic touchpad movement and click emulation.
- Bluetooth loading through GoldHEN LaunchPad and AutoRun, with user selection deferred until the DS3 connects.
- Input-loss handling, USB reconnection, and clean session shutdown.
- Build tools, local tests, and installation instructions.

The release ELF uses Bluetooth and requires an already paired DS3. USB is available as a separate build. Tested on firmware 13.52 with GoldHEN v2.4b18.12; Bluetooth remains experimental. Rumble, player LEDs, and initial pairing are not included. See [README.md](README.md) for compatibility and remaining tests.
