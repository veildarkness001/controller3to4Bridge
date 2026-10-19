#!/usr/bin/env python3
"""Download pinned official tools into work/. No system-wide installation."""
import hashlib
import platform
from pathlib import Path
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_COMMIT = "573b4a0d0d5fa8695cfeee5f5e2f0447c550035d"


def download(url, name, digest):
    path = ROOT / "work/downloads" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "DualShockToPS4-build"})
        with urllib.request.urlopen(request, timeout=60) as response, path.open("wb") as out:
            while chunk := response.read(1024 * 1024):
                out.write(chunk)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"SHA256 mismatch: {path}; remove the incomplete archive and retry")
    return path


def main():
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise SystemExit("This bootstrap currently supports Apple Silicon macOS only")
    toolchain = ROOT / "work/toolchain"
    toolchain.mkdir(parents=True, exist_ok=True)
    sdk = toolchain / "ps4-payload-sdk"
    archive = download(
        "https://github.com/ps4-payload-dev/sdk/releases/download/v0.9/ps4-payload-sdk.zip",
        "ps4-payload-sdk-v0.9.zip",
        "3190e88255a72161e5ce2f22a8ae8f638a2c5a9d1ff1e0209ed692d36b4d9ec5")
    if not sdk.exists():
        with zipfile.ZipFile(archive) as bundle:
            for name in bundle.namelist():
                if not (toolchain / name).resolve().is_relative_to(toolchain.resolve()):
                    raise SystemExit("Unsafe archive path")
            bundle.extractall(toolchain)
    zig = toolchain / "zig-macos-aarch64-0.13.0"
    archive = download(
        "https://ziglang.org/download/0.13.0/zig-macos-aarch64-0.13.0.tar.xz",
        "zig-macos-aarch64-0.13.0.tar.xz",
        "46fae219656545dfaf4dce12fb4e8685cec5b51d721beee9389ab4194d43394c")
    if not zig.exists():
        with tarfile.open(archive) as bundle:
            bundle.extractall(toolchain, filter="data")
    source = ROOT / "work/ps4-payload-sdk-src"
    if not source.exists():
        subprocess.run(["git", "clone", "--no-checkout",
                        "https://github.com/ps4-payload-dev/sdk.git", str(source)], check=True)
        subprocess.run(["git", "-C", str(source), "checkout", "--detach", SDK_COMMIT], check=True)
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != SDK_COMMIT:
        raise SystemExit(f"SDK source revision must be {SDK_COMMIT}; found {actual}. No checkout was overwritten.")
    print("Pinned SDK v0.9 + firmware-capable CRT source and Zig/LLD available locally")


if __name__ == "__main__":
    main()
