# Controller3To4Bridge

## What is it?

Controller3To4Bridge lets you use a Sony DualShock 3 on a PS4 running GoldHEN, with standard controls and basic touchpad emulation. It runs on the console, so you can play without a computer, phone, or network connection.

**One ELF supports both USB and internal Bluetooth** in version 1.2.3. Connect the DS3 with a mini-USB data cable for wired play, or disconnect the cable and press **PS** for Bluetooth. USB takes priority when a cable is connected. The bridge clears held inputs and starts a fresh controller session when changing transport. Choose your user again if prompted; the ELF stays running.

The 1.2.3 ELF was tested with **PS4 firmware 13.52, GoldHEN v2.4b18.12, and an original Sony DS3**: USB → Bluetooth → USB → Bluetooth, user selection and game menus after each change, brief wireless gameplay, and clean shutdown. Longer sessions, rest mode, other firmware versions, and third-party controllers remain unverified.

Bluetooth requires a DS3 already paired to the PS4; initial pairing is not included. USB does not require Bluetooth pairing. Rumble is not implemented.

## Controls

| DualShock 3 | PS4 action |
| --- | --- |
| Face buttons, D-pad, sticks, shoulder buttons, and triggers | Same controls |
| PS | PS |
| Start | Options |
| Hold Select | Touch the center of the touchpad |
| Select + right stick | Move across the touchpad |
| Select + R3 | Click the touchpad |
| Press and release Select without moving or clicking | Share |

Release Select to use the right stick normally again. Touchpad emulation supports one contact.

**Choose USB or Bluetooth before opening a game.** Changing transport recreates the virtual controller; some applications may lose input until reopened. Switching worked in the tested game, but compatibility with other games is not established.

## Install with GoldHEN and enable AutoRun

1. Use `controller-3-to-4-bridge-1.2.3.elf`, available locally in `outputs/`. The same file works for both connection types. Published downloads are listed in [Releases](https://github.com/veildarkness001/controller3to4Bridge/releases).
2. Enable GoldHEN's FTP server. From an FTP client on a computer on the same network, connect to the PS4's IP address on port **2121** and copy the ELF to **`/data/payloads/`**.
3. On the PS4, open **GoldHEN → Utilities → PayLoader LaunchPad**, select the ELF, and press **X** to start it once.
4. Connect the DS3 with a mini-USB data cable, or press **PS** on an already paired wireless DS3. Choose a user when prompted. If you also use a DS4, assign it to a different user.
5. To enable automatic startup, highlight the ELF in LaunchPad and press **Square** to add it to **AutoRun**. On subsequent restarts, the bridge starts when you activate GoldHEN. Connect by USB or press **PS** for Bluetooth.

**Run one bridge instance at a time.** After adding it to AutoRun, do not also launch it manually. When upgrading, disable the old entry in **GoldHEN → Utilities → AutoRun Queue** before adding the new ELF. Square in LaunchPad adds an entry; it does not disable the previous version.

For Bluetooth, hold **PS** and open **Sound/Devices → Turn Off Device** to turn off the DS3. It appears as **DUALSHOCK 4**; pressing **PS** activates it again. With USB connected, this action disables virtual input until you unplug the cable. A physical DS4 can have the same menu name, so the name alone does not distinguish them. One Bluetooth menu power-off and PS reactivation cycle was confirmed on PS4 with 1.2.2. Wired menu power-off and coexistence with an active physical DS4 remain untested.

See [CHANGELOG.md](CHANGELOG.md) for release changes.

## Run manually from a computer

You need **Python 3**, the ELF, and a computer on the same network as the PS4.

1. Activate GoldHEN and enable its **BinLoader/PayLoader server** on port **9090**. Disable the bridge's AutoRun entry and make sure no bridge instance is running; disabling AutoRun does not stop an instance already loaded.
2. Open a terminal in the folder containing the ELF. Replace `PS4_IP` below with the console's IP address and run the command once:

```sh
python3 -c "import socket; from pathlib import Path; data = Path('controller-3-to-4-bridge-1.2.3.elf').read_bytes(); s = socket.create_connection(('PS4_IP', 9090), timeout=8); s.sendall(data); s.shutdown(socket.SHUT_WR); s.close()"
```

Use `python` instead of `python3` if that is your Python 3 command.

3. Connect the DS3 by USB, or press **PS** for Bluetooth, and choose a user. Once connected, the computer is no longer needed.

This loads the ELF for the current session; it does not add it to AutoRun. If the command fails or the DS3 does not connect, check the running bridge before sending it again.

## Project structure

```text
include/dt/        Shared interfaces and controller state
src/
  app/             USB and Bluetooth bridges and session management
  protocol/        DS3 input decoding
  transport/       Automatic USB/Bluetooth selection and Bluetooth transport
  platform/ps4/    PS4 USB, Bluetooth, and virtual controller support
  output/          PS4 control mapping and touchpad emulation
tests/             Local tests
tools/             Build, test, and manual console tools
vendor/            Third-party source and licenses
work/              Local dependencies, temporary files, and logs (ignored by Git)
outputs/           Built ELF files and checksums (ignored by Git)
```

## Acknowledgements

This project builds on work shared by the PS4 homebrew and controller development communities. Thank you to the following authors and contributors:

- **[SiSTR0](https://github.com/SiSTR0)**, creator of **[GoldHEN](https://github.com/GoldHEN/GoldHEN)**, for the homebrew environment and services used to run and test this bridge.
- **[MoHadiShibli](https://github.com/MoHadiShibli)**, author of **[Control4Free](https://github.com/MoHadiShibli/Control4Free)**. This project includes three source/header files from Control4Free 1.2.2 and adapts its virtual controller initialization, lifecycle, and event handling for local DS3 input.
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

GPL-3.0. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for licensing details and third-party notices.

This independent project is not affiliated with or endorsed by Sony Interactive Entertainment. DUALSHOCK, PlayStation, and PS4 are trademarks or registered trademarks of Sony Interactive Entertainment Inc.
