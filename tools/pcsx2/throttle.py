# demo only: relays 0.0.0.0:8097 -> 127.0.0.1:8096, server-to-client paced (PCSX2's sockets-mode TCP stalls on bursts)
import asyncio, sys
RATE = int(sys.argv[1]) if len(sys.argv) > 1 else 700_000
async def pipe(r, w, rate):
    try:
        while True:
            d = await r.read(1460 * 8)
            if not d: break
            w.write(d); await w.drain()
            if rate: await asyncio.sleep(len(d) / rate)
    except Exception: pass
    finally:
        try: w.close()
        except Exception: pass
async def handle(cr, cw):
    sr, sw = await asyncio.open_connection('127.0.0.1', 8096)
    await asyncio.gather(pipe(cr, sw, 0), pipe(sr, cw, RATE))
async def main():
    s = await asyncio.start_server(handle, '0.0.0.0', 8097)
    async with s: await s.serve_forever()
asyncio.run(main())
