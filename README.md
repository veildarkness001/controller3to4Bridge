# Controller3To4Bridge

## What is it?

Controller3To4Bridge lets you use a Sony DualShock 3 and DualSense (PS5) on a PS4 running GoldHEN, with standard controls and basic touchpad support. It runs on the console, so you can play without a computer, phone, or network connection.

**One ELF supports both USB and internal Bluetooth.** DS3 support over both transports began in version 1.3.0; version 1.4.0 adds the DualSense. Connect the DS3 or DualSense with a USB data cable for wired play, or disconnect the cable and press **PS** for Bluetooth. USB takes priority when a cable is connected. The bridge clears held inputs and starts a fresh controller session when changing transport. Choose your user again if prompted; the ELF stays running.

The 1.2.3 ELF was tested with **PS4 firmware 13.52, GoldHEN v2.4b18.12, and an original Sony DS3**: USB → Bluetooth → USB → Bluetooth, user selection and game menus after each change, brief wireless gameplay, and clean shutdown. Longer sessions, rest mode, other firmware versions, and third-party controllers remain unverified.

For Bluetooth, the DS3 must already be paired with the PS4. To register a DualSense for the first time, start the bridge with the controller unplugged, allow its discovery slot, then open **Settings → Devices → Bluetooth Devices** on the PS4. Hold **Create + PS** on the DualSense, select **Wireless Controller**, and accept **Yes** promptly. After registration, press **PS** to reconnect and choose a user. USB does not require Bluetooth pairing. Rumble and haptic feedback are not implemented.

| Controller | USB | Internal Bluetooth |
| --- | --- | --- |
| Original Sony DS3 | Supported | Automatic discovery, already paired |
| Original Sony DualSense | PS4 13.52 and checked revision 0x0100 only | Automatic discovery, basic controls |

DualSense basic Bluetooth mode maps face buttons, D-pad, sticks, shoulders, analog triggers and PS to PS4. Create maps directly to Share, Options to Options, and the touchpad button maps to its click. Create leaves the right stick and R3 independent. **Touch positions, gestures and motion are unavailable in this mode.** USB uses the full 64-byte report and maps native touch contacts; touch positions and gestures have not been separately verified on console. Automatic requests for enhanced Bluetooth mode remain disabled by default because continuous wireless input has only been established in basic mode. Raw motion samples are not converted to PS4 motion input.

DualSense Edge and third-party devices are excluded. Do not use DS3 pairing tools on a DualSense. Battery display, microphone/mute actions, speaker/headset audio, rumble/haptics, adaptive trigger effects and DualSense LED control are not forwarded.

## Controls

| DualShock 3 | DualSense | PS4 action |
| --- | --- | --- |
| Face buttons, D-pad, sticks, shoulder buttons, and triggers | Face buttons, D-pad, sticks, shoulder buttons, and triggers | Same controls |
| PS | PS | PS |
| Start | Options | Options |
| Hold Select | Touchpad contact (USB only) | Touch the touchpad |
| Select + right stick | Move a touchpad contact (USB only) | Move across the touchpad |
| Select + R3 | Touchpad button | Click the touchpad |
| Press and release Select without moving or clicking | Create | Share |

On the DS3, release Select to use the right stick normally again; its touchpad emulation supports one contact. DualSense touch positions and gestures over USB have not been separately verified on console. Bluetooth provides only the touchpad button click.

**Choose USB or Bluetooth before opening a game.** Changing transport recreates the virtual controller; some applications may lose input until reopened. Switching worked in the tested game, but compatibility with other games is not established.

## Install with GoldHEN and enable AutoRun

1. Download the version you want from [Releases](https://github.com/veildarkness001/controller3to4Bridge/releases).
2. Enable GoldHEN's FTP server. From an FTP client on a computer on the same network, connect to the PS4's IP address on port **2121** and copy the ELF to **`/data/payloads/`**.
3. On the PS4, open **GoldHEN → Utilities → PayLoader LaunchPad**, select the ELF, and press **X** to start it once.
4. Connect the DS3 or DualSense with a USB data cable, or press **PS** on a controller already registered for Bluetooth. Choose a user when prompted. If you also use a DS4, assign it to a different user.
5. To enable automatic startup, highlight the ELF in LaunchPad and press **Square** to add it to **AutoRun**. On subsequent restarts, the bridge starts when you activate GoldHEN. Connect by USB or press **PS** for Bluetooth.

**Run one bridge instance at a time.** After adding it to AutoRun, do not also launch it manually. When upgrading, disable the old entry in **GoldHEN → Utilities → AutoRun Queue** before adding the new ELF. Square in LaunchPad adds an entry; it does not disable the previous version.

For Bluetooth, hold **PS** and open **Sound/Devices → Turn Off Device** to turn off the virtual controller. It appears as **DUALSHOCK 4**; pressing **PS** on the DS3 or DualSense activates it again. With USB connected, this action disables virtual input until you unplug the cable. A physical DS4 can have the same menu name, so the name alone does not distinguish them. Wired menu power-off and coexistence with an active physical DS4 remain untested.

The bridge searches for DS3 and DualSense Bluetooth input automatically. Before acquiring input it alternates ten-second listening slots, fully restoring one native backend before starting the other. Once valid input arrives it keeps that model for the session, with no session time limit. Initial discovery can take a slot plus connection/cleanup time; if a controller stops trying before its slot, press PS again. Only one controller is selected at a time.

No profile edit is required: a missing **`/data/dualshock_to_ps4/bluetooth-controller`** or the value `auto` uses automatic discovery, starting with the last model selected during this process (DS3 initially). Existing `ds3` and `dualsense` files are accepted as a first-model preference; both models remain eligible. Invalid configuration still stops activation. USB takes priority without Bluetooth pairing; DS3 wins when both wired models are attached.

See [CHANGELOG.md](CHANGELOG.md) for release changes.

## Run manually from a computer

You need **Python 3**, the ELF, and a computer on the same network as the PS4.

1. Activate GoldHEN and enable its **BinLoader/PayLoader server** on port **9090**. Disable the bridge's AutoRun entry and make sure no bridge instance is running; disabling AutoRun does not stop an instance already loaded.
2. Open a terminal in the folder containing the ELF. Replace `PS4_IP` below with the console's IP address and run the command once:

```sh
python3 -c "import socket; from pathlib import Path; data = Path('controller-3-to-4-bridge-1.4.0.elf').read_bytes(); s = socket.create_connection(('PS4_IP', 9090), timeout=8); s.sendall(data); s.shutdown(socket.SHUT_WR); s.close()"
```

Use `python` instead of `python3` if that is your Python 3 command.

3. Connect the DS3 or DualSense by USB, or press **PS** on an already registered controller for Bluetooth, and choose a user. Once connected, the computer is no longer needed.

This loads the ELF for the current session; it does not add it to AutoRun. If the command fails or the controller does not connect, check the running bridge before sending it again.

## Project structure

```text
include/dt/        Shared interfaces and controller state
src/
  app/             Bridges, controller profile and session management
  protocol/        DS3 and DualSense input decoding
  transport/       Automatic USB/Bluetooth selection and Bluetooth transport
  platform/ps4/    PS4 USB, Bluetooth, and virtual controller support
  output/          PS4 control mapping and touchpad emulation
tests/             Local tests
tools/             Build, test, and manual console tools
vendor/            Third-party source and licenses
work/              Local dependencies, temporary files, and logs (ignored by Git)
outputs/           Built ELF files and checksums (ignored by Git)
```

Local verification and build (does not deploy to the console):

```sh
python3 tools/build.py test
python3 tools/build.py release --preserve-latest
```

The current console checks establish basic control for both models over USB and Bluetooth on one PS4/controller setup. Long-term stability, rest-mode recovery, broader firmware/revision compatibility, simultaneous physical DS4 coexistence, and full DualSense touch/motion behavior remain unverified.

## Acknowledgements

This project builds on work shared by the PS4 homebrew and controller development communities. Thank you to the following authors and contributors:

- **[SiSTR0](https://github.com/SiSTR0)**, creator of **[GoldHEN](https://github.com/GoldHEN/GoldHEN)**, for the homebrew environment and services used to run and test this bridge.
- **[MoHadiShibli](https://github.com/MoHadiShibli)**, author of **[Control4Free](https://github.com/MoHadiShibli/Control4Free)**. This project includes three source/header files from Control4Free 1.2.2 and adapts its virtual controller initialization, lifecycle, and event handling for local DS3 input.
- **[seregonwar](https://github.com/seregonwar)**, author of **[SplashDown](https://github.com/seregonwar/SplashDown)**, whose virtual controller work underlies the implementation credited by Control4Free.
- **John Törnblom and the [ps4-payload-sdk contributors](https://github.com/ps4-payload-dev/sdk)**, for the SDK, startup code, and runtime used to build the payloads; and the **[libusb authors](https://github.com/libusb/libusb)**, whose descriptor declarations informed the reduced USB ABI declarations.
- **The [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and [shadPS4](https://github.com/shadps4-emu/shadPS4) contributors**, for references used to check PS4 USB interfaces, cleanup signatures, and timeout behavior.
- **[nefarius and the DsHidMini contributors](https://github.com/nefarius/DsHidMini)**, together with the **[SDL](https://github.com/libsdl-org/SDL) and [Linux HID](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c) contributors**, for DS3 report layouts and initialization references, including the explicit USB F4 activation command.
- **Sony Interactive Entertainment and the Linux hid-playstation contributors**, and the **SDL HIDAPI PS5 contributors**, for public DualSense report and transport references.
- **Kristian Sloth Lauszus and the [USB Host Shield 2.0 contributors](https://github.com/felis/USB_Host_Shield_2.0)**, the **[BlueZ contributors](https://github.com/bluez/bluez)**, and the authors of **[sixpair](https://github.com/lakkatv/sixpair)**, for DS3 Bluetooth framing, activation, and USB pairing references.
- **[idc](https://github.com/idc)** for **[ps4libdoc](https://github.com/idc/ps4libdoc)**, and the **[SharpEmu contributors](https://github.com/sharpemu/sharpemu)**, for references consulted during research into native Bluetooth interfaces.
- **[crucifix86](https://github.com/crucifix86)**, author of **[xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)**, for an additional reference on PS4 USB controller access.
- **The [Zig](https://ziglang.org/) and [Capstone](https://github.com/capstone-engine/capstone) contributors**, for the local linker and disassembly tools used during development.

The protocol and API references above informed this project's implementation. Incorporated files, adaptations, source revisions, and licensing details are documented in [THIRD_PARTY.md](THIRD_PARTY.md) and [vendor/control4free/README.md](vendor/control4free/README.md).

## License

GPL-3.0. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for licensing details and third-party notices.
