"""Windows real-executable UTF-8 argv regression; no converter-only oracle."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import uuid


def run(executable, *arguments, timeout=15):
    result = subprocess.run(
        [str(executable), *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=timeout,
    )
    return result, result.stdout.decode("utf-8"), result.stderr.decode("utf-8")


def check_invalid_utf16(executable):
    for invalid in ("\ud800", "\udc00"):
        result, out, err = run(executable, invalid)
        if result.returncode != 2 or "Invalid UTF-16 command line" not in err:
            raise AssertionError(f"malformed UTF-16 was not rejected: {result.returncode} {out!r} {err!r}")


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        out, err = process.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        out, err = process.communicate(timeout=3)
    if process.returncode != 0:
        print(f"child exit={process.returncode} stdout={out!r} stderr={err!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("this regression must launch the Windows executable")
    check_invalid_utf16(args.exe)
    name = "\u65e5\u672c \U0001f680 \"252\"-" + uuid.uuid4().hex[:8]
    with tempfile.TemporaryDirectory(prefix="nozzle-argv-") as directory:
        sender_json = Path(directory) / "sender.json"
        receiver_json = Path(directory) / "receiver.json"
        sender = subprocess.Popen([
            str(args.exe), "--smoke-sender", "--source", name,
            "--width", "320", "--height", "240", "--frames", "600",
            "--interval-ms", "16", "--timeout-ms", "5000",
            "--evidence", str(sender_json),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            received, out, err = run(args.exe, "--smoke-receiver", "--source", name,
                "--width", "320", "--height", "240", "--min-frames", "2",
                "--timeout-ms", "10000", "--evidence", str(receiver_json))
            if received.returncode != 0:
                raise AssertionError(f"receiver failed: {out!r} {err!r}")
            record = json.loads(receiver_json.read_text(encoding="utf-8"))
            if record["verdict"] != "PASS" or record["sender_name"] != name or record["connected_sender_name"] != name:
                raise AssertionError(f"receiver identity/pixels did not match: {record!r}")
            out, err = sender.communicate(timeout=20)
            if sender.returncode != 0:
                raise AssertionError(f"sender failed: {out!r} {err!r}")
            record = json.loads(sender_json.read_text(encoding="utf-8"))
            if record["verdict"] != "PASS" or record["sender_name"] != name:
                raise AssertionError(f"sender identity did not match: {record!r}")
        finally:
            stop(sender)
    print("PASS Windows Unicode sender/receiver exact identity and smoke pixels; strict rejection")


if __name__ == "__main__":
    main()
