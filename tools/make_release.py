#!/usr/bin/env python3
"""Build the release zip: dist/ORBIT-<version>.zip (run after build.sh APP=launcher, tools/build_neutrino.sh and
build.sh orbit_hash). Usage: python tools/make_release.py v1.0 <neutrino dev release dir> <udpfs_server.py>

Layout, to be copied to the root of the USB drive (server/ goes to the game server instead):
  launcher.elf  neutrino/ (official dev release + ORBIT's neutrino.elf, ee_core.elf, raagent.irx)  DVD/  CD/
  server/ (orbit_catalog.py, orbit_hash, udpfs_server.py, systemd units)  tools/covers.py  QUICKSTART.txt  LICENSE
"""
import hashlib, os, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ver, ndev, udpfs = sys.argv[1], sys.argv[2], sys.argv[3]
name = f"ORBIT-{ver}"
out = os.path.join(ROOT, "dist", name + ".zip")

QUICKSTART = f"""ORBIT {ver} - quick start
============================

1. Copy launcher.elf, neutrino/, DVD/ and CD/ to the root of a FAT32 or exFAT USB drive.
2. Put your PS2 ISOs in DVD/ (CD games in CD/). PS1 games: POPStarter VCDs in POPS/. Homebrew: APPS/.
3. On the PS2, start mass0:/launcher.elf (uLaunchELF, or FreeMcBoot's autoboot).
4. Pick a game and press X. The first boot writes mass0:/orbit/config.ini with every setting, commented.

Buttons: left/right (or up/down) move, L1/R1 filter, square changes the view, triangle per-game options,
circle achievements, X play, SELECT data overlay.
In a game: L1+L2+R1+R2+START+SELECT opens the menu (restart, power off).

Optional
- English: in config.ini set  [ui] idioma = en  (Spanish is the default).
- Covers: plug in the network cable; the missing ones download at boot.
  Your own: tools/covers.py turns PNG/JPG files named by serial (SLUS-21376.png) into the .c16 pair for
  mass0:/covers/ (python3 covers.py IN_DIR OUT_DIR; needs numpy and Pillow).
  No DNS on your router? Set  [red] dns = 1.1.1.1  in config.ini.
- Back to ORBIT from a game: ORBIT installs BOOT/ORBIT.ELF on your memory card; set it as FMCB's autoboot.
- Achievements: run the xeRAbora client v0.1.0-alpha.9, signed in, on your network
  (github.com/hacan359/xerabora/releases/tag/v0.1.0-alpha.9: the version ORBIT is tested with).
- Games from a home server: see server/README.txt (wired network needed).

Website and setup video: https://danielnuld.github.io/ps2-launcher/
Source: https://github.com/danielnuld/ps2-launcher (GPL-3.0; the Neutrino fork is AFL-3.0)
"""

SERVER = """ORBIT game server (Linux)
=========================

Serves your ISOs to the PS2 over the network, with udpfs (Neutrino's) and ORBIT's catalog.

1. Put the ISOs in <root>/DVD (and <root>/CD), e.g. /srv/data/ps2/DVD.
2. Copy this folder to the server, e.g. ~/orbit, and install both services (user services):
     mkdir -p ~/.config/systemd/user && cp *.service ~/.config/systemd/user/
     edit the --root in both if your folder is not /srv/data/ps2
     systemctl --user daemon-reload && systemctl --user enable --now udpfs orbit-catalog
     loginctl enable-linger $USER      (keep them running without a login)
3. Allow UDP 62966 (udpfs) and TCP 18290 (catalog) in the firewall.
4. On the PS2, config.ini:  [juegos] origen = usb, udpfs   and   servidor = <server IP>
5. Use a cable: udpfs does not survive WiFi packet loss.

orbit_hash is a static x86_64 Linux binary (the launcher's own ISO and memory-card code). orbit_catalog.py needs
only Python 3. curl http://<server>:18290/catalog lists what the PS2 will see.
"""

UNITS = {
    "orbit-catalog.service": """[Unit]
Description=ORBIT game catalog for the PS2 launcher
After=network-online.target

[Service]
Environment=PYTHONUNBUFFERED=1
ExecStart=/usr/bin/python3 %h/orbit/orbit_catalog.py --root /srv/data/ps2 --port 18290
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
""",
    "udpfs.service": """[Unit]
Description=UDPFS server for the PS2 (Neutrino)
After=network-online.target

[Service]
Environment=PYTHONUNBUFFERED=1
ExecStart=/usr/bin/python3 %h/orbit/udpfs_server.py --root-dir /srv/data/ps2
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
""",
}

os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    def add(src, arc, mode=0o644):
        info = zipfile.ZipInfo.from_file(src, f"{name}/{arc}")
        info.external_attr = (mode | 0o100000) << 16
        with open(src, "rb") as f:
            z.writestr(info, f.read(), zipfile.ZIP_DEFLATED)

    def text(arc, s, mode=0o644):
        info = zipfile.ZipInfo(f"{name}/{arc}", (2026, 10, 4, 0, 0, 0))
        info.external_attr = (mode | 0o100000) << 16
        z.writestr(info, s.replace("\n", "\r\n") if arc.endswith(".txt") else s, zipfile.ZIP_DEFLATED)

    add(os.path.join(ROOT, "launcher.elf"), "launcher.elf")
    overlay = {"neutrino.elf", "modules/ee_core.elf"}  # ORBIT's fork over the official release
    for dirpath, _, files in os.walk(ndev):
        for f in files:
            rel = os.path.relpath(os.path.join(dirpath, f), ndev).replace("\\", "/")
            if rel.startswith("udpfs_server"):
                continue
            add(os.path.join(ROOT, "dist", "neutrino", rel) if rel in overlay else os.path.join(dirpath, f), "neutrino/" + rel)
    add(os.path.join(ROOT, "dist", "neutrino", "modules", "raagent.irx"), "neutrino/modules/raagent.irx")
    add(os.path.join(ROOT, "third_party", "neutrino-igr", "LICENSE"), "neutrino/LICENSE")
    text("DVD/put your PS2 DVD ISOs here.txt", "")
    text("CD/put your PS2 CD ISOs here.txt", "")
    add(os.path.join(ROOT, "tools", "covers.py"), "tools/covers.py", 0o755)
    add(os.path.join(ROOT, "tools", "orbit_catalog.py"), "server/orbit_catalog.py", 0o755)
    add(os.path.join(ROOT, "orbit_hash"), "server/orbit_hash", 0o755)
    add(udpfs, "server/udpfs_server.py", 0o755)
    for n, s in UNITS.items():
        text("server/" + n, s)
    text("server/README.txt", SERVER)
    text("QUICKSTART.txt", QUICKSTART)
    add(os.path.join(ROOT, "LICENSE"), "LICENSE")

h = hashlib.sha256(open(out, "rb").read()).hexdigest().upper()
print(out, os.path.getsize(out), "bytes")
print("launcher.elf SHA-256", hashlib.sha256(open(os.path.join(ROOT, "launcher.elf"), "rb").read()).hexdigest().upper())
print("zip SHA-256", h)
