#!/usr/bin/env python3
"""Self-update assets (#15) for a release: update.txt (signed as update.sig) and ORBIT's own files, each a loose
release asset. Usage: python tools/make_update.py v1.1 [OUT_DIR] [--only launcher.elf,...]

update.txt:  version = v1.1
             file = <asset> <path on the USB> <size> <sha256>
The signature (ECDSA P-256 over SHA-256, DER) is made on the game server, where the private key lives
(nuld:~/.orbit/update-key.pem), and checked here against tools/update-key.pub.pem, the key inside the launcher
(update.c). Upload everything in OUT_DIR to the release next to the zip.
"""
import hashlib, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = [  # asset, path on the USB, built file
    ("launcher.elf", "launcher.elf", "launcher.elf"),
    ("neutrino.elf", "neutrino/neutrino.elf", "dist/neutrino/neutrino.elf"),
    ("ee_core.elf", "neutrino/modules/ee_core.elf", "dist/neutrino/modules/ee_core.elf"),
    ("raagent.irx", "neutrino/modules/raagent.irx", "dist/neutrino/modules/raagent.irx"),
]
KEY_HOST, KEY = "nuld", "~/.orbit/update-key.pem"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--only")]
    only = next((a.split("=", 1)[1].split(",") for a in sys.argv[1:] if a.startswith("--only=")), None)
    ver = args[0]
    out = args[1] if len(args) > 1 else os.path.join(ROOT, "dist", f"ORBIT-{ver}-update")
    src = open(os.path.join(ROOT, "launcher.c"), encoding="utf-8").read()
    built = re.search(r'#define ORBIT_VERSION "([^"]+)"', src).group(1)
    if built != ver and not only:
        sys.exit(f"launcher.c ORBIT_VERSION is {built}, not {ver}: bump it and rebuild")
    os.makedirs(out, exist_ok=True)
    lines = [f"version = {ver}"]
    for asset, dest, path in FILES:
        if only and asset not in only:
            continue
        data = open(os.path.join(ROOT, path), "rb").read()
        shutil.copyfile(os.path.join(ROOT, path), os.path.join(out, asset))
        lines.append(f"file = {asset} {dest} {len(data)} {hashlib.sha256(data).hexdigest()}")
    text = ("\n".join(lines) + "\n").encode()
    open(os.path.join(out, "update.txt"), "wb").write(text)
    sig = subprocess.run(["ssh", KEY_HOST, f"openssl dgst -sha256 -sign {KEY}"], input=text, capture_output=True,
                         check=True).stdout
    open(os.path.join(out, "update.sig"), "wb").write(sig)
    check = subprocess.run(["openssl", "dgst", "-sha256", "-verify", os.path.join(ROOT, "tools", "update-key.pub.pem"),
                            "-signature", os.path.join(out, "update.sig"), os.path.join(out, "update.txt")],
                           capture_output=True, text=True)
    if "Verified OK" not in check.stdout:
        sys.exit("signature check FAILED: " + check.stdout + check.stderr)
    print(text.decode(), end="")
    print(f"signed and verified; upload {out}/* to the release")


main()
