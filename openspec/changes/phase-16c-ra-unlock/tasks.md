## 1. Agent

- [ ] 1.1 raagent: receive on its port while the game runs, `RAU1` → event record (seq, id, points) DMA'd into the mailbox, seq last
- [ ] 1.2 Bench: the client's `--test-unlock` (a fake unlock every 20 s) reaches the mailbox in PCSX2 (fork log)

## 2. ee_core

- [ ] 2.1 VBLANK: new seq → pulse (PMODE / BGCOLOR, xeRAbora timing), duplicate id within 60 frames ignored; restore `ALP 0xFF` and black at the end
- [ ] 2.2 Session list of unlocked ids after the mailbox
- [ ] 2.3 Size check in `ee_core.map`; console boot check of the new ee_core

## 3. Gate (console)

- [ ] 3.1 Black with `--test-unlock` on the client: one pulse per notice, in native and 480p; game keeps running (frame counter in the snapshot header keeps rising)
- [ ] 3.2 A real unlock in Black: pulse within a second of the client's unlock log line; `docs/phase16c-results.md`
