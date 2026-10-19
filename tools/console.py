#!/usr/bin/env python3
"""Explicit manual PS4 test operations. No AutoRun, retries or discovery scan."""
import argparse
from datetime import datetime, timezone
from ftplib import FTP, error_perm
import hashlib
import io
import json
from pathlib import Path
import socket

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["stage", "send", "fetch", "stop"])
    parser.add_argument("--kind", choices=["bridge", "diagnostic", "probe", "bluetooth-probe", "bluetooth-bridge"], default="bridge")
    parser.add_argument("--host", required=True)
    parser.add_argument("--run", required=True, help="Local run identifier (directory below work/runs)")
    args = parser.parse_args()
    if args.kind not in ("bridge", "bluetooth-bridge") and args.action == "stop":
        parser.error("This diagnostic has a bounded duration and no stop-file handler")
    if not args.run or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_." for c in args.run) or args.run in (".", ".."):
        raise SystemExit("Invalid run name")
    run = ROOT / "work/runs" / args.run
    run.mkdir(parents=True, exist_ok=True)
    metadata_name, label, header = {
        "bridge": ("usb-bridge", "usb-bridge", "USB bridge "),
        "bluetooth-bridge": ("bluetooth-bridge", "bluetooth-bridge", "Bluetooth bridge "),
        "diagnostic": ("usb-diagnostic", "usb-diag", "USB diagnostic "),
        "probe": ("usb-probe", "usb-probe", "USB probe "),
        "bluetooth-probe": ("bluetooth-probe", "bluetooth-probe", "Bluetooth probe "),
    }[args.kind]
    log_name = metadata_name + ".log"
    final_marker = "bridge finished status=" if args.kind in ("bridge", "bluetooth-bridge") else "finished status="
    meta = json.loads((ROOT / "outputs" / (metadata_name + "-build.json")).read_text())
    name = f"dualshock-to-ps4-{label}-{meta['version']}.elf"
    data = (ROOT / "outputs" / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != meta["sha256"]:
        raise SystemExit("ELF does not match build metadata")
    record_path = run / "deployment.json"
    record = json.loads(record_path.read_text()) if record_path.exists() else {}
    if record and record.get("sha256") != meta["sha256"]:
        raise SystemExit("Use a new run name for a different binary")
    record.update(version=meta["version"], sha256=meta["sha256"], remote_path="/data/payloads/" + name)

    def save():
        record["updated_at_utc"] = datetime.now(timezone.utc).isoformat()
        record_path.write_text(json.dumps(record, indent=2) + "\n")

    if args.action == "send":
        if record.get("state") in ("sending", "sent", "send-failed-may-be-partial"):
            raise SystemExit("This run already attempted execution; inspect its log before any new run")
        record["state"] = "sending"; save()
        try:
            with socket.create_connection((args.host, 9090), timeout=8) as sock:
                sock.sendall(data); sock.shutdown(socket.SHUT_WR)
        except OSError:
            record["state"] = "send-failed-may-be-partial"; save(); raise
        record["state"] = "sent"; record["sent_at_utc"] = datetime.now(timezone.utc).isoformat(); save()
        print(f"Sent {name} once ({len(data)} bytes); execution still needs log confirmation")
        return

    with FTP() as ftp:
        ftp.connect(args.host, 2121, timeout=8); ftp.login()
        if args.action == "stage":
            try: ftp.cwd("/data/payloads")
            except error_perm:
                ftp.mkd("/data/payloads"); ftp.cwd("/data/payloads")
            previous = io.BytesIO()
            try: ftp.retrbinary("RETR " + name, previous.write)
            except error_perm: pass
            if previous.getvalue() and hashlib.sha256(previous.getvalue()).hexdigest() != meta["sha256"]:
                raise SystemExit("A different binary already uses this remote version name; bump VERSION")
            if not previous.getvalue(): ftp.storbinary("STOR " + name, io.BytesIO(data))
            check = io.BytesIO(); ftp.retrbinary("RETR " + name, check.write)
            if hashlib.sha256(check.getvalue()).hexdigest() != meta["sha256"]:
                raise SystemExit("Remote verification failed; not executing")
            record["ftp_roundtrip_verified"] = True; save()
            if args.kind != "bridge":
                # Preserve the remote previous result before a probe truncates it.
                old_path = run / ("before-" + log_name)
                if not old_path.exists():
                    old = io.BytesIO()
                    try: ftp.retrbinary("RETR /data/dualshock_to_ps4/" + log_name, old.write)
                    except error_perm: pass
                    if old.getvalue(): old_path.write_bytes(old.getvalue())
            print(f"Staged and verified {name}: {meta['sha256']}")
        elif args.action == "fetch":
            previous = io.BytesIO()
            try: ftp.retrbinary("RETR /data/dualshock_to_ps4/" + log_name + ".1", previous.write)
            except error_perm: pass
            log = io.BytesIO(); ftp.retrbinary("RETR /data/dualshock_to_ps4/" + log_name, log.write)
            raw = previous.getvalue() + log.getvalue(); (run / log_name).write_bytes(raw)
            text = raw.decode(errors="replace")
            record["execution_confirmed"] = record.get("execution_confirmed", False) or text.startswith(header + meta["version"])
            record["finished"] = final_marker in text
            save()
            lines = text.splitlines()
            milestones = [line for line in lines if not line.startswith("input ")]
            print("\n".join(milestones[-16:]))
            samples = [line for line in lines if line.startswith("input ")]
            print(f"Logged input snapshots: {len(samples)}")
            print("\n".join(samples[-5:]))
        else:
            ftp.storbinary("STOR /data/dualshock_to_ps4/stop", io.BytesIO(b"stop\n"))
            record["stop_requested"] = True; save()
            print("Requested local stop; fetch log to confirm device cleanup and privilege restoration")


if __name__ == "__main__":
    main()
