#pragma once
// Game sources (phase 14): where PS2 ISOs are looked for besides the launcher's USB.
enum { SRC_USB, SRC_HDD, SRC_HDL, SRC_MX4SIO, SRC_ILINK, SRC_MMCE, SRC_UDPBD, SRC_UDPFS, SRC_N };
#define SRC_MAX 8
typedef struct { char type, unit; char root[10]; } source; // root: "mass0:", "ata0:", "mmce1:", "hdd0:"; "udpfs:" is not mounted (phase 17: the catalog)
extern source src[SRC_MAX];
extern int nsrc;
extern const char *src_err[SRC_N]; // why each wanted source did not come up, NULL if it did (#4: SELECT overlay)
extern const char *const src_key[SRC_N];   // config.ini names
extern const char *const src_bsd[SRC_N];   // Neutrino -bsd= / path prefix
extern const char *const src_label[SRC_N]; // chip text

int src_home(const char *root); // #8: mounts an MMCE home ("mmce0:"); 0 for "mass0:" or no card
unsigned src_mask(const char *list); // "usb, hdd, mx4sio" -> 1 << SRC_*; hdd also tries HD Loader (APA) disks
// Loads the drivers of every source in mask (not the home, src[0]: mass0:, or the MMCE ORBIT runs from) and adds the ones that
// answer. ip: fixed IP for udpbd / udpfs (ministack, from Neutrino's modules/ in ndir). Returns NULL or the first
// problem, in Spanish, for a toast.
const char *src_init(unsigned mask, const char *ip, const char *ndir, int udpfs_mount); // udpfs_mount: no catalog (#10)
// HD Loader partitions on hdd0: (APA): add(ctx, partition, title, startup "SLUS_213.76") per game
int src_hdl_scan(void (*add)(void *ctx, const char *part, const char *title, const char *startup), void *ctx);
void src_udpfs_ip(const char *ndir, const char *ip); // bsd-udpfs.toml's ip= (phase 17)
void src_mmce_game(const char *startup); // MMCE (SD2PSX, MemCard PRO 2): switch to the game's own card
