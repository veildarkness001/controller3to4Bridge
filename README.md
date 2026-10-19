# Controller3To4Bridge

Use a Sony DualShock 3 directly on a PS4 running GoldHEN.

Controller3To4Bridge is an experimental on-console bridge that reads the DS3 over USB or internal Bluetooth and presents it to the PS4 as a virtual DualShock 4. Once running, it needs no computer, phone, or network connection during gameplay. It also maps DS3 controls to basic touchpad gestures for games and system menus that need them.

This is an independent project, not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment. DUALSHOCK, PlayStation, and PS4 are trademarks or registered trademarks of Sony Interactive Entertainment Inc. Product names are used to identify compatibility.

**USB and experimental internal Bluetooth are working in hardware tests, including RetroArch gameplay and touchpad emulation over Bluetooth.**

## Status and compatibility

| Feature | Status |
| --- | --- |
| USB controller input and PS button | Tested on console |
| USB connection after startup and reconnection | Tested with bridge 0.3.3 |
| Touchpad movement, click, and Share mapping | Tested with bridge 0.4.0 in the PS4 system keyboard and menus |
| Internal Bluetooth | User assignment, menu navigation, and PS verified with 0.12.0; RetroArch gameplay and touchpad emulation confirmed with 0.12.1 on firmware 13.52 |
| Rumble and other controller feedback | Not implemented |

The tested setup is **PS4 firmware 13.52, GoldHEN v2.4b18.12, an original Sony DS3, and a mini-USB data cable**. Compatibility with other firmware versions or third-party controllers has not been established.

A confirmed 30–60 minute gameplay session and a hardware test of input loss while holding Select are still pending. Simulated tests do not establish long-term hardware stability.

The DS3 has no physical touchpad, speaker, headset jack, or light bar. Touchpad emulation provides one contact with fixed sensitivity; full DS4 feature parity is outside the current implementation.

## Controls

| DS3 input | PS4 output |
| --- | --- |
| Face buttons, D-pad, sticks, shoulder buttons, and triggers | Corresponding controls |
| PS | PS |
| Start | Options |
| Hold Select | Touch contact starting at the center of the touchpad |
| Select + right stick | Move the touch contact |
| Select + R3 | Touchpad click |
| Press and release Select without moving or clicking | Share |

While Select is held, the right stick controls the emulated touchpad. Input loss cancels the touch contact; Select must be released before touchpad input can resume.

## Build and test

The build bootstrap currently supports **Apple Silicon macOS** and requires Python 3, Git, `make`, and Apple clang. It downloads pinned SDK and Zig versions into `work/`, verifies their hashes, and does not install tools globally.

```sh
python3 tools/bootstrap_toolchain.py
python3 tools/build.py test
python3 tools/build.py bridge
```

ELF payloads and build metadata are written to `outputs/`. Dependencies, temporary files, and logs are kept in `work/`. Both directories are excluded from Git. Building does not send or run a payload on the console.

Local tests cover the DS3 protocol, control mapping, sessions, transports, and diagnostics. C tests use AddressSanitizer and UndefinedBehaviorSanitizer; Python tests cover the console tooling.

## Run manually

The USB bridge requires a PS4 running GoldHEN with its payload loader enabled. The host tools use FTP on port **2121** and the payload loader on port **9090** to upload, start, and inspect a run. Replace `PS4_IP` with your console's address and choose a new run name for each execution:

```sh
python3 tools/console.py stage --kind bridge --host PS4_IP --run usb-test-001
python3 tools/console.py send --kind bridge --host PS4_IP --run usb-test-001
python3 tools/console.py fetch --kind bridge --host PS4_IP --run usb-test-001
```

`stage` verifies the uploaded file using SHA256. `send` sends the payload once. Connect the DS3 with a mini-USB data cable and complete the PS4 user assignment if prompted.

To stop the bridge and retrieve its final log:

```sh
python3 tools/console.py stop --kind bridge --host PS4_IP --run usb-test-001
python3 tools/console.py fetch --kind bridge --host PS4_IP --run usb-test-001
```

Confirm that the log reports bridge completion, virtual controller removal, and privilege restoration before starting another instance. Keep AutoRun disabled during prototype testing.

Bluetooth diagnostics are separate experimental payloads. Some change controller pairing or temporarily modify firmware-specific behavior; review their implementation and procedure before running them.

The experimental native Bluetooth bridge is built with `python3 tools/build.py bluetooth-bridge` and uses `--kind bluetooth-bridge` in the console tool. It requires a DS3 already paired to the console. The first hardware test, version 0.12.0, stopped after 180 seconds with clean device removal and restoration. Version 0.12.1 runs until manual stop, user logout, or a fatal stream/platform error; short RetroArch gameplay and touchpad emulation have been confirmed, while prolonged stability and physical reconnection remain pending. It temporarily modifies firmware 13.52 HID admission, the native DS3 rejection, and report capacity; it is not yet a production coexistence design for other Bluetooth devices. Controller LEDs and rumble are not configured. Keep AutoRun disabled.

## Project structure

- `include/dt/`: transport, controller state, session, and virtual output interfaces.
- `src/protocol/`: DS3 report decoding and Bluetooth framing.
- `src/transport/`: Bluetooth transport, independent of the platform link adapter.
- `src/platform/ps4/`: USB, native Bluetooth, virtual controller support, and diagnostics.
- `src/output/`: PS4 control mapping and virtual device events.
- `src/app/`: bridge, session management, and diagnostic entry points.
- `tests/`: local tests using simulated devices and services.
- `tools/`: builds, manual console operations, and ELF inspection.
- `vendor/`: attributed Control4Free source files and their license.

## Acknowledgements

This project builds on work shared by the PS4 homebrew and controller development communities. Thank you to the following authors and contributors:

- **[SiSTR0](https://github.com/SiSTR0)**, creator of **[GoldHEN](https://github.com/GoldHEN/GoldHEN)**, for the homebrew environment and services used to run and test this bridge.
- **[MoHadiShibli](https://github.com/MoHadiShibli)**, author of **[Control4Free](https://github.com/MoHadiShibli/Control4Free)**. This project includes three source/header files from Control4Free 1.2.1 and adapts its virtual controller initialization, lifecycle, and event handling for local DS3 input.
- **[seregonwar](https://github.com/seregonwar)**, author of **[SplashDown](https://github.com/seregonwar/SplashDown)**, whose virtual controller work underlies the implementation credited by Control4Free.
- **John Törnblom and the [ps4-payload-sdk contributors](https://github.com/ps4-payload-dev/sdk)**, for the SDK, startup code, and runtime used to build the payloads; and the **[libusb authors](https://github.com/libusb/libusb)**, whose descriptor declarations informed the reduced USB ABI declarations.
- **The [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and [shadPS4](https://github.com/shadps4-emu/shadPS4) contributors**, for references used to check PS4 USB interfaces, cleanup signatures, and timeout behavior.
- **[nefarius and the DsHidMini contributors](https://github.com/nefarius/DsHidMini)**, together with the **[SDL](https://github.com/libsdl-org/SDL) and [Linux HID](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c) contributors**, for DS3 report layouts and initialization references, including the explicit USB F4 activation command.
- **Kristian Sloth Lauszus and the [USB Host Shield 2.0 contributors](https://github.com/felis/USB_Host_Shield_2.0)**, the **[BlueZ contributors](https://github.com/bluez/bluez)**, and the authors of **[sixpair](https://github.com/lakkatv/sixpair)**, for DS3 Bluetooth framing, activation, and USB pairing references.
- **[idc](https://github.com/idc)** for **[ps4libdoc](https://github.com/idc/ps4libdoc)**, and the **[SharpEmu contributors](https://github.com/sharpemu/sharpemu)**, for references consulted during research into native Bluetooth interfaces.
- **[crucifix86](https://github.com/crucifix86)**, author of **[xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)**, for an additional reference on PS4 USB controller access.
- **The [Zig](https://ziglang.org/) and [Capstone](https://github.com/capstone-engine/capstone) contributors**, for the local linker and disassembly tools used during development.

The protocol and API references above informed this project's implementation. Incorporated files, adaptations, source revisions, and licensing details are documented in [THIRD_PARTY.md](THIRD_PARTY.md) and [vendor/control4free/README.md](vendor/control4free/README.md).

## License

GPL-3.0. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for third-party notices and licensing details.
