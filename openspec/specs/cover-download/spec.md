# cover-download Specification

## Purpose
TBD - created by archiving change phase-8-covers-net. Update Purpose after archive.
## Requirements
### Requirement: Network config
`mass0:/orbit/config.ini` SHALL accept two sections:
- `[red]`: `ip = dhcp` (the default), or a fixed IPv4 address with `mascara`, `puerta` and `dns`;
- `[portadas]`: `descargar = si` (the default) or `no`.

With `descargar = no`, or when no cover is missing, the network modules SHALL NOT be loaded. A newly created
`config.ini` SHALL contain both sections with their defaults and a comment per key.

#### Scenario: Nothing to download
- **WHEN** every game has its cover pair on the USB
- **THEN** the launcher boots exactly as before, with no network modules loaded

### Requirement: Download of missing covers
When downloads are on and at least one game has no valid cover pair, the launcher SHALL, after the home screen
appears:
1. start the network: DHCP, or the fixed address;
2. for each game without covers, GET
   `https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/<SERIAL>.jpg`, the same source as
   `tools/fetch_covers.py`;
3. accept only a 200 response whose body length matches its Content-Length and that decodes as a JPEG;
4. convert it on the EE with the `covers.py` steps:
   - resize to 256×368 and 184×264 in linear light with a Lanczos filter;
   - quantize to RGB555 with Floyd–Steinberg;
5. write the pair to `mass0:/covers/` and show the cover in the carousel without a restart.

Input and animation SHALL stay responsive during the whole download. A 404 SHALL leave the generic cover. A network
or TLS error SHALL stop the run.

#### Scenario: First download
- **WHEN** Black has no cover on the USB and the console has internet through its cable
- **THEN** Black's cover appears in the carousel, `mass0:/covers/SLUS-21376.c16` and `_s.c16` exist, and the next boot
  loads them from the USB

#### Scenario: Same picture as the PC tool
- **WHEN** the host selftest converts SLUS-21376.jpg with the console code and compares it with `covers.py` output
- **THEN** no channel value differs by more than 1 level of 31

#### Scenario: No internet
- **WHEN** the cable is out or DNS / TLS fails
- **THEN** the status line says so, the launcher keeps working with the generic covers, and no file is written

### Requirement: Download status
While a download runs, or after it fails, the home screen SHALL show one status line:
- connecting;
- `n / m` covers;
- the error: no link, no address, DNS, connection, TLS or protocol.

The line SHALL disappear a few seconds after a successful finish.

#### Scenario: Progress
- **WHEN** 3 covers are missing and the first one has arrived
- **THEN** the line reads 1 / 3

### Requirement: Gate
On the console with internet, the phase SHALL pass if all of the following hold:
- the covers missing from the USB download and appear;
- frame time keeps 0 missed vsyncs during the download, measured with the launcher's frame windows;
- a game launched afterwards (Black) still starts through Neutrino with the network modules loaded.

`docs/phase8-results.md` SHALL record the time per cover (download + conversion) and the missed vsyncs.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file holds the measured time per cover, the missed vsync count, and the Neutrino launch result

