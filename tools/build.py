#!/usr/bin/env python3
"""Host tests or a PS4 USB diagnostic ELF. Never deploys or enables AutoRun."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK_COMMIT = "573b4a0d0d5fa8695cfeee5f5e2f0447c550035d"


def run(args):
    print(shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def tests():
    build = ROOT / "work/tests"
    build.mkdir(parents=True, exist_ok=True)
    common = ["clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-fsanitize=address,undefined", "-Iinclude", "-Ivendor/control4free"]
    for name, sources, flags in [
        ("bridge-activation", ["src/app/bridge_session.c", "src/protocol/ds3.c",
         "src/output/ps4_mapping.c", "tests/bridge_activation_test.c"], ["-Dusleep=dt_test_usleep"]),
        ("ps4-bluetooth", ["tests/ps4_bluetooth_test.c", "src/platform/ps4/bluetooth_native_reports.c", "src/protocol/ds3_bluetooth.c", "src/protocol/ds3.c"],
         ["-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dread=dt_test_read", "-Dclose=dt_test_close", "-Dioctl=dt_test_ioctl", "-Dusleep=dt_test_usleep", "-Dkqueue=dt_test_kqueue", "-Dkevent=dt_test_kevent"]),
        ("bluetooth-native-reports", ["src/platform/ps4/bluetooth_native_reports.c", "src/protocol/ds3_bluetooth.c", "src/protocol/ds3.c", "tests/bluetooth_native_reports_test.c"], ["-Dioctl=dt_test_ioctl"]),
        ("bluetooth-report-window", ["tests/bluetooth_profile_window_test.c"], ["-Itests/include", "-DDT_BT_IGNORE_NATIVE_DS3", "-DDT_BT_ACTIVATE_REPORTS"]),
        ("bluetooth-native-ignore-cleanup", ["src/platform/ps4/bluetooth_native_probe.c", "tests/bluetooth_native_probe_test.c"],
         ["-DDT_BT_IGNORE_NATIVE_DS3", "-DDT_BT_KNOWN_EVENTS", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dioctl=dt_test_ioctl",
          "-Dread=dt_test_read", "-Dclose=dt_test_close", "-Dusleep=dt_test_usleep", "-Dkqueue=dt_test_kqueue", "-Dkevent=dt_test_kevent"]),
        ("bluetooth-native-ignore-window", ["tests/bluetooth_profile_window_test.c"], ["-Itests/include", "-DDT_BT_IGNORE_NATIVE_DS3"]),
        ("bluetooth-callback-code-probe", ["tests/bluetooth_code_probe_test.c"],
         ["-Itests/include", "-DDT_BT_CODE_CALLBACKS", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dclose=dt_test_close"]),
        ("bluetooth-profile-window", ["tests/bluetooth_profile_window_test.c"], ["-Itests/include"]),
        ("bluetooth-incoming-window", ["tests/bluetooth_scan_window_test.c"], ["-Dioctl=dt_test_ioctl"]),
        ("bluetooth-event-capability", ["tests/bluetooth_event_privileges_test.c"], ["-Itests/include"]),
        ("bluetooth-cache-rejections", ["tests/bluetooth_cache_probe_test.c"],
         ["-Itests/include", "-DDT_BT_CACHE_REJECTIONS", "-Dopen=dt_test_open", "-Dread=dt_test_read", "-Dclose=dt_test_close"]),
        ("bluetooth-cache-probe", ["tests/bluetooth_cache_probe_test.c"],
         ["-Itests/include", "-Dopen=dt_test_open", "-Dread=dt_test_read", "-Dclose=dt_test_close"]),
        ("bluetooth-pairing-probe", ["tests/bluetooth_pairing_probe_test.c"],
         ["-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dread=dt_test_read", "-Dfsync=dt_test_fsync", "-Dclose=dt_test_close"]),
        ("bluetooth-host-probe", ["tests/bluetooth_host_probe_test.c"],
         ["-Itests/include", "-Dopen=dt_test_open", "-Dioctl=dt_test_ioctl", "-Dclose=dt_test_close"]),
        ("bluetooth-code-range-probe", ["tests/bluetooth_code_probe_test.c"],
         ["-Itests/include", "-DDT_BT_CODE_TABLE", "-DDT_BT_TABLE_OFFSET=0x1000", "-DDT_BT_TABLE_SIZE=8192", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dclose=dt_test_close"]),
        ("bluetooth-admission", ["tests/bluetooth_admission_test.c"], ["-Itests/include", "-Dioctl=dt_test_ioctl"]),
        ("bluetooth-code-table-probe", ["tests/bluetooth_code_probe_test.c"],
         ["-Itests/include", "-DDT_BT_CODE_TABLE", "-DDT_BT_TABLE_OFFSET=0x1100", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dclose=dt_test_close"]),
        ("bluetooth-code-probe", ["tests/bluetooth_code_probe_test.c"],
         ["-Itests/include", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dclose=dt_test_close"]),
        ("bluetooth-code-detail-probe", ["tests/bluetooth_code_probe_test.c"],
         ["-Itests/include", "-DDT_BT_CODE_DETAIL", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dclose=dt_test_close"]),
        ("bluetooth-privileged-probe", ["src/platform/ps4/bluetooth_privileged_probe.c", "tests/bluetooth_privileged_probe_test.c"], ["-Itests/include"]),
        ("bluetooth-native-cache-on-event", ["src/platform/ps4/bluetooth_native_probe.c", "tests/bluetooth_native_probe_test.c"],
         ["-DDT_BT_ALL_EVENTS", "-DDT_BT_LINK_SNAPSHOT", "-DDT_BT_CACHE_ON_EVENT", "-DDT_BT_CACHE_AROUND_LISTEN", "-DDT_BT_KNOWN_EVENTS", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dioctl=dt_test_ioctl",
          "-Dread=dt_test_read", "-Dclose=dt_test_close", "-Dusleep=dt_test_usleep", "-Dkqueue=dt_test_kqueue", "-Dkevent=dt_test_kevent"]),
        ("bluetooth-native-all-events", ["src/platform/ps4/bluetooth_native_probe.c", "tests/bluetooth_native_probe_test.c"],
         ["-DDT_BT_ALL_EVENTS", "-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dioctl=dt_test_ioctl",
          "-Dread=dt_test_read", "-Dclose=dt_test_close", "-Dusleep=dt_test_usleep", "-Dkqueue=dt_test_kqueue", "-Dkevent=dt_test_kevent"]),
        ("bluetooth-native-probe", ["src/platform/ps4/bluetooth_native_probe.c", "tests/bluetooth_native_probe_test.c"],
         ["-Dopen=dt_test_open", "-Dwrite=dt_test_write", "-Dioctl=dt_test_ioctl",
          "-Dread=dt_test_read", "-Dclose=dt_test_close", "-Dusleep=dt_test_usleep", "-Dkqueue=dt_test_kqueue", "-Dkevent=dt_test_kevent"]),
        ("bluetooth", ["src/protocol/ds3_bluetooth.c", "src/protocol/ds3.c",
         "src/transport/bluetooth.c", "src/app/bridge_session.c", "src/output/ps4_mapping.c",
         "tests/bluetooth_test.c"], ["-Dusleep=dt_test_usleep"]),
        ("bluetooth-probe", ["src/platform/ps4/bluetooth_probe.c", "tests/bluetooth_probe_test.c"],
         ["-Itests/include", "-DDT_BT_SOCKET_FAMILY=36", "-Dsocket=dt_test_socket", "-Dclose=dt_test_close",
          "-Dstat=dt_test_stat"]),
        ("log", ["src/platform/ps4/log.c", "tests/log_test.c"], ["-Itests/include",
         '-DDT_LOG_DIRECTORY="work/tests/logs"']),
        ("bridge-session", ["src/app/bridge_session.c", "src/protocol/ds3.c",
         "src/output/ps4_mapping.c", "tests/bridge_session_test.c"], ["-Dusleep=dt_test_usleep"]),
        ("ds3", ["src/protocol/ds3.c", "tests/ds3_test.c"], []),
        ("usb", ["src/platform/ps4/usb.c", "tests/usb_test.c"], ["-Itests/include",
         "-Ddlopen=dt_test_dlopen", "-Ddlsym=dt_test_dlsym", "-Ddlerror=dt_test_dlerror",
         "-Ddlclose=dt_test_dlclose"]),
        ("usb-probe", ["src/platform/ps4/usb.c", "tests/usb_test.c"], ["-Itests/include", "-DDT_USB_PROBE",
         "-Ddlopen=dt_test_dlopen", "-Ddlsym=dt_test_dlsym", "-Ddlerror=dt_test_dlerror",
         "-Ddlclose=dt_test_dlclose"]),
        ("usb-probe64", ["src/platform/ps4/usb.c", "tests/usb_test.c"], ["-Itests/include", "-DDT_USB_PROBE", "-DDT_USB_PROBE_PACKET64",
         "-Ddlopen=dt_test_dlopen", "-Ddlsym=dt_test_dlsym", "-Ddlerror=dt_test_dlerror",
         "-Ddlclose=dt_test_dlclose"]),
        ("mapping", ["src/output/ps4_mapping.c", "tests/mapping_test.c"], []),
        ("vda-events", ["src/output/vda_events.c", "vendor/control4free/klog_line.c", "tests/vda_events_test.c"], []),
        ("virtual-pad", ["src/platform/ps4/virtual_pad.c", "src/output/ps4_mapping.c",
         "src/output/vda_events.c", "vendor/control4free/klog_line.c", "tests/virtual_pad_test.c"],
         ["-Itests/include", "-Dopen=dt_test_open", "-Dread=dt_test_read", "-Dclose=dt_test_close",
          "-Dusleep=dt_test_usleep", "-Ddlopen=dt_test_dlopen", "-Ddlerror=dt_test_dlerror"]),
    ]:
        binary = build / (name + "-test")
        run(common + flags + sources + ["-o", binary])
        run([binary])

    run([sys.executable, "tests/console_test.py"])


BT_VERSIONS = {
    "bluetooth-probe": "0.1.0", "bluetooth-native-probe": "0.2.0",
    "bluetooth-privileged-probe": "0.2.1", "bluetooth-code-probe": "0.3.0",
    "bluetooth-code-detail-probe": "0.3.1", "bluetooth-code-table-probe": "0.3.2",
    "bluetooth-admission-probe": "0.4.0", "bluetooth-code-driver-probe": "0.4.1",
    "bluetooth-code-tables-probe": "0.4.2", "bluetooth-host-probe": "0.5.0",
    "bluetooth-code-hci-probe": "0.5.1", "bluetooth-code-hci-tables-probe": "0.5.2",
    "bluetooth-pairing-probe": "0.6.0", "bluetooth-pairing-restore": "0.6.1",
    "bluetooth-radio-probe": "0.7.0", "bluetooth-radio-events-probe": "0.7.1",
    "bluetooth-radio-ready-probe": "0.7.2", "bluetooth-radio-signal-probe": "0.7.3",
    "bluetooth-cache-probe": "0.8.0", "bluetooth-radio-cache-probe": "0.8.1",
    "bluetooth-code-access-probe": "0.8.2", "bluetooth-radio-privileged-probe": "0.8.3",
    "bluetooth-cache-rejections-probe": "0.8.4", "bluetooth-radio-discovery-probe": "0.8.5", "bluetooth-radio-incoming-probe": "0.9.0", "bluetooth-radio-known-probe": "0.9.1", "bluetooth-radio-profile-probe": "0.10.0",
}
STATIC_RANGES = {
    "bluetooth-code-dispatch-probe": (0x5d2000, 0x10000),
    "bluetooth-code-channel-probe": (0x5bb000, 0x9000),
    "bluetooth-code-access-probe": (0x3b2c80, 0x800),
    "bluetooth-code-table-probe": (0xad5cb8, 144),
    "bluetooth-code-driver-probe": (0x5a6000, 0x15000),
    "bluetooth-code-tables-probe": (0xad5c80, 0x600),
    "bluetooth-code-hci-probe": (0x5a1000, 0x5000),
    "bluetooth-code-hci-tables-probe": (0xad4e10, 0x240),
}
BT_VERSIONS["bluetooth-code-channel-probe"] = "0.10.1"
BT_VERSIONS["bluetooth-radio-link-probe"] = "0.10.2"
BT_VERSIONS["bluetooth-code-callbacks-probe"] = "0.10.3"
BT_VERSIONS["bluetooth-code-dispatch-probe"] = "0.10.4"
BT_VERSIONS["bluetooth-radio-native-ignore-probe"] = "0.11.0"
BT_VERSIONS["bluetooth-radio-reports-probe"] = "0.11.1"


def payload(target):
    release = target == "release"
    bridge = target == "bridge"
    bluetooth_bridge = target in ("bluetooth-bridge", "release")
    probe = target.startswith("probe")
    wait = target in ("probe-wait", "probe64", "probe-f4")
    packet64, f4 = target == "probe64", target == "probe-f4"
    bluetooth_probe = target in BT_VERSIONS
    bluetooth_radio = target in ("bluetooth-radio-probe", "bluetooth-radio-events-probe", "bluetooth-radio-ready-probe", "bluetooth-radio-signal-probe", "bluetooth-radio-cache-probe", "bluetooth-radio-privileged-probe", "bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe")
    bluetooth_admission = target == "bluetooth-admission-probe" or bluetooth_radio
    bluetooth_privileged = target == "bluetooth-privileged-probe"
    bluetooth_native = target == "bluetooth-native-probe" or bluetooth_privileged or bluetooth_admission
    bluetooth_code = target.startswith("bluetooth-code-")
    bluetooth_code_detail = target == "bluetooth-code-detail-probe"
    bluetooth_code_table = target in STATIC_RANGES
    bluetooth_host = target == "bluetooth-host-probe"
    bluetooth_restore = target == "bluetooth-pairing-restore"
    bluetooth_pairing = target == "bluetooth-pairing-probe" or bluetooth_restore
    version = (ROOT / "VERSION").read_text().strip() if release else "0.12.2" if bluetooth_bridge else BT_VERSIONS[target] if bluetooth_probe else "0.1.5" if f4 else "0.1.4" if packet64 else "0.1.3" if wait else "0.1.2" if probe else (ROOT / "VERSION").read_text().strip()
    if not version or any(c not in "0123456789." for c in version):
        raise SystemExit("Invalid VERSION")
    source = ROOT / "work/ps4-payload-sdk-src"
    sdk = ROOT / "work/toolchain/ps4-payload-sdk"
    zig = ROOT / "work/toolchain/zig-macos-aarch64-0.13.0/zig"
    if not sdk.exists() or not zig.exists() or not source.exists():
        raise SystemExit("Run python3 tools/bootstrap_toolchain.py first")
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != SDK_COMMIT:
        raise SystemExit("Unexpected SDK CRT revision; refusing to build")
    clang = shutil.which("clang")
    if not clang:
        raise SystemExit("clang is required")
    tools = ROOT / "work/toolchain/bin"
    tools.mkdir(parents=True, exist_ok=True)
    linker = tools / "ld.lld"
    linker.write_text("#!/bin/sh\nexec " + shlex.quote(str(zig)) + ' ld.lld "$@"\n')
    linker.chmod(0o755)
    config = tools / "llvm-config"
    config.write_text("#!/bin/sh\nprintf '%s\\n' " + shlex.quote(str(tools)) + "\n")
    config.chmod(0o755)
    # Build the pinned CRT with the native Apple clang cross-compiler and LLD.
    run(["make", "-C", source / "crt", f"CC={clang}", f"LD={linker}",
         f"LLVM_CONFIG={config}", f"DESTDIR={sdk}", "install"])
    kind = "bluetooth-bridge" if bluetooth_bridge else "bluetooth-probe" if bluetooth_probe else "usb-probe" if probe else "usb-bridge" if bridge else "usb-diagnostic"
    build = ROOT / "work/build" / kind
    build.mkdir(parents=True, exist_ok=True)
    files = ["src/app/usb_diagnostic.c", "src/protocol/ds3.c",
             "src/platform/ps4/usb.c", "src/platform/ps4/log.c"]
    if bluetooth_probe:
        files = ["src/app/bluetooth_probe.c", "src/platform/ps4/bluetooth_probe.c", "src/platform/ps4/log.c"]
        if target in ("bluetooth-cache-probe", "bluetooth-cache-rejections-probe"):
            files[1] = "src/platform/ps4/bluetooth_cache_probe.c"
        if bluetooth_pairing:
            files[1] = "src/platform/ps4/bluetooth_pairing_probe.c"
            files += ["src/platform/ps4/bluetooth_host_probe.c", "src/platform/ps4/usb.c", "src/protocol/ds3.c"]
        if bluetooth_host:
            files[1] = "src/platform/ps4/bluetooth_host_probe.c"
        if bluetooth_native:
            files[1] = "src/platform/ps4/bluetooth_native_probe.c"
        if target in ("bluetooth-radio-privileged-probe", "bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"):
            files.append("src/platform/ps4/bluetooth_event_privileges.c")
        if bluetooth_privileged:
            files.append("src/platform/ps4/bluetooth_privileged_probe.c")
        if bluetooth_code:
            files[1] = "src/platform/ps4/bluetooth_code_probe.c"
        if bluetooth_admission:
            files.append("src/platform/ps4/bluetooth_admission.c")
        if target in ("bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"):
            files.append("src/platform/ps4/bluetooth_profile_window.c")
        if target == "bluetooth-radio-incoming-probe":
            files.append("src/platform/ps4/bluetooth_scan_window.c")
        if target in ("bluetooth-radio-cache-probe", "bluetooth-radio-privileged-probe", "bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"):
            files.append("src/platform/ps4/bluetooth_cache_probe.c")
    if target == "bluetooth-radio-reports-probe":
        files += ["src/platform/ps4/bluetooth_native_reports.c", "src/protocol/ds3_bluetooth.c", "src/protocol/ds3.c"]
    if bridge:
        files[0] = "src/app/usb_bridge.c"
        files += ["src/app/bridge_session.c", "src/platform/ps4/virtual_pad.c", "src/output/ps4_mapping.c",
                  "src/output/vda_events.c", "vendor/control4free/klog_line.c"]
    if bluetooth_bridge:
        files = ["src/app/bluetooth_bridge.c", "src/platform/ps4/bluetooth.c",
                 "src/platform/ps4/bluetooth_native_reports.c", "src/platform/ps4/bluetooth_event_privileges.c",
                 "src/platform/ps4/bluetooth_cache_probe.c", "src/platform/ps4/bluetooth_admission.c",
                 "src/platform/ps4/bluetooth_profile_window.c", "src/protocol/ds3_bluetooth.c", "src/protocol/ds3.c",
                 "src/app/bridge_session.c", "src/platform/ps4/virtual_pad.c", "src/output/ps4_mapping.c",
                 "src/output/vda_events.c", "vendor/control4free/klog_line.c", "src/platform/ps4/log.c"]
    objects = []
    flags = ["-target", "x86_64-scei-ps4", "--sysroot=" + str(sdk),
             "-isystem", str(sdk / "target/include"), "-Iinclude", "-Ivendor/control4free", "-std=gnu11",
             "-Wall", "-Wextra", "-Werror", "-O2", "-g", "-fPIC",
             "-fno-stack-protector", "-fno-plt", "-femulated-tls",
             "-fvisibility-nodllstorageclass=default", f'-DDT_VERSION="{version}"']
    if release:
        flags += ["-g0", f"-ffile-prefix-map={ROOT}=."]
    if target in ("bluetooth-radio-privileged-probe", "bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_EVENT_PRIVILEGES"]
    if target in ("bluetooth-cache-probe", "bluetooth-cache-rejections-probe"): flags += ["-DDT_BT_CACHE_PROBE"]
    if target in ("bluetooth-radio-cache-probe", "bluetooth-radio-privileged-probe", "bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_CACHE_ON_EVENT"]
    if target == "bluetooth-cache-rejections-probe": flags += ["-DDT_BT_CACHE_REJECTIONS"]
    if target in ("bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_CACHE_AROUND_LISTEN"]
    if target == "bluetooth-radio-incoming-probe": flags += ["-DDT_BT_OPEN_PAIRING"]
    if target in ("bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_KNOWN_EVENTS"]
    if target in ("bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_PROFILE_WINDOW"]
    if target == "bluetooth-radio-link-probe": flags += ["-DDT_BT_LINK_SNAPSHOT"]
    if target in ("bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_IGNORE_NATIVE_DS3", "-DDT_BT_LINK_SNAPSHOT"]
    if target == "bluetooth-radio-reports-probe": flags += ["-DDT_BT_ACTIVATE_REPORTS"]
    if bluetooth_radio: flags += ["-DDT_BT_LISTEN_SECONDS=60u"]
    if bluetooth_pairing: flags += ["-DDT_BT_PAIRING_RESTORE" if bluetooth_restore else "-DDT_BT_PAIRING_PROBE"]
    if bluetooth_host: flags += ["-DDT_BT_HOST_PROBE"]
    if probe: flags += ["-DDT_USB_PROBE"]
    if wait: flags += ["-DDT_USB_PROBE_WAIT"]
    if packet64: flags += ["-DDT_USB_PROBE_PACKET64"]
    if f4: flags += ["-DDT_USB_PROBE_F4"]
    if bluetooth_bridge: flags += ["-DDT_BT_CAPABILITIES_ONLY", "-DDT_BT_IGNORE_NATIVE_DS3", "-DDT_BT_ACTIVATE_REPORTS"]
    if bluetooth_native: flags += ["-DDT_BT_NATIVE_PROBE"]
    if bluetooth_privileged: flags += ["-DDT_BT_PRIVILEGED_PROBE"]
    if bluetooth_code: flags += ["-DDT_BT_CODE_PROBE"]
    if target == "bluetooth-code-callbacks-probe": flags += ["-DDT_BT_CODE_CALLBACKS"]
    if bluetooth_code_detail: flags += ["-DDT_BT_CODE_DETAIL"]
    if bluetooth_code_table: flags += ["-DDT_BT_CODE_TABLE"]
    if bluetooth_admission: flags += ["-DDT_BT_ADMISSION_PROBE"]
    if bluetooth_code_table:
        offset, size = STATIC_RANGES[target]
        flags += [f"-DDT_BT_TABLE_OFFSET={offset}", f"-DDT_BT_TABLE_SIZE={size}"]
    if target in ("bluetooth-radio-events-probe", "bluetooth-radio-ready-probe", "bluetooth-radio-signal-probe", "bluetooth-radio-cache-probe", "bluetooth-radio-privileged-probe", "bluetooth-radio-discovery-probe", "bluetooth-radio-incoming-probe", "bluetooth-radio-known-probe", "bluetooth-radio-profile-probe", "bluetooth-radio-link-probe", "bluetooth-radio-native-ignore-probe", "bluetooth-radio-reports-probe"): flags += ["-DDT_BT_ALL_EVENTS"]
    for name in files:
        obj = build / (name.replace("/", "_") + ".o")
        run([clang] + flags + ["-c", name, "-o", obj])
        objects.append(obj)
    label = "bluetooth-bridge" if bluetooth_bridge else "bluetooth-probe" if bluetooth_probe else "usb-probe" if probe else "usb-bridge" if bridge else "usb-diag"
    output = ROOT / (f"outputs/controller-3-to-4-bridge-{version}.elf" if release
                     else f"outputs/dualshock-to-ps4-{label}-{version}.elf")
    output.parent.mkdir(parents=True, exist_ok=True)
    libraries = ["-lc", "-lkernel_web", "-lSceLibcInternal", "-lSceNet", "-ldl"]
    if bridge or bluetooth_bridge: libraries += ["-lScePad", "-lSceUserService"]
    run([linker, *(["--strip-debug"] if release else []), "-m", "elf_x86_64", "-pie", "--eh-frame-hdr", "-z", "max-page-size=0x4000",
         "-T", sdk / "ldscripts/elf_x86_64.x", "-L", sdk / "target/lib",
         sdk / "target/lib/crt1.o", *objects, *libraries, "-o", output])
    data = output.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 62:
        raise SystemExit("Output is not an x86-64 little-endian ELF")
    # This SDK intentionally puts _start at virtual address zero in ET_DYN.
    # Zero is valid: check the entry against an executable PT_LOAD instead.
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    entry = header[4]
    executable_entry = False
    for i in range(header[10]):
        segment = struct.unpack_from("<IIQQQQQQ", data, header[5] + i * header[9])
        if segment[0] == 1 and segment[1] & 1 and segment[3] <= entry < segment[3] + segment[6]:
            executable_entry = True
    if header[1] != 3 or not executable_entry:
        raise SystemExit("ELF must be ET_DYN with entry in an executable PT_LOAD")
    sha = hashlib.sha256(data).hexdigest()
    output.with_suffix(".elf.sha256").write_text(f"{sha}  {output.name}\n")
    meta = {"version": version, "kind": kind, "sdk": "v0.9",
            "crt_commit": SDK_COMMIT, "linker": "Zig 0.13.0 / LLD 18.1.6",
            "compiler": subprocess.check_output([clang, "--version"], text=True).splitlines()[0],
            "sha256": sha, "bytes": len(data), "hardware_tested": False,
            "sources_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                               for name in files + [str(p.relative_to(ROOT)) for p in sorted((ROOT / "include").rglob("*.h"))]
                               + ["src/platform/ps4/usbd_abi.h"]
                               + (["src/platform/ps4/bluetooth_native_abi.h"] if bluetooth_native or bluetooth_bridge else [])
                               + (["vendor/control4free/c4f_sce.h", "vendor/control4free/c4f_vda.h"] if bridge or bluetooth_bridge else [])}}
    serialized = json.dumps(meta, indent=2) + "\n"
    if release:
        output.with_suffix(".build.json").write_text(serialized)
    else:
        (ROOT / "outputs" / (kind + "-build.json")).write_text(serialized)
        (ROOT / "outputs" / (kind + "-" + version + "-build.json")).write_text(serialized)
    print(f"Built {output.name}: {len(data)} bytes, SHA256 {sha}; NOT tested on PS4")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=["test", "release", "payload", "bridge", "probe", "probe-wait", "probe64", "probe-f4", "bluetooth-bridge", *BT_VERSIONS])
    args = parser.parse_args()
    tests() if args.target == "test" else payload(args.target)
