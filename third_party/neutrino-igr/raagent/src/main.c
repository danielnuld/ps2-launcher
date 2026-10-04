/*
  raagent.irx - RetroAchievements agent for the ORBIT fork of Neutrino (phase 16b), on Neutrino's ministack.

  Loaded into the game by the launcher's -cfg=ra (smap, ministack ip=<ps2 ip>, raagent srv=<client> me=<ps2 ip>);
  the loader appends mbox=<hex>, the EE mailbox in module storage. At start the agent DMAs the address of its
  snapshot buffer into mailbox[0]; ee_core (ra.c) then DMAs one snapshot per frame into that buffer. The agent
  announces the console to xeRAbora's client (RAP1 until RAO1), sends every new, untorn snapshot as RA15 packets and
  a heartbeat (RAH1) every ten seconds. Wire format: xeRAbora protocol/PROTOCOL.md and OPL modules/network/raudp
  (AFL-3.0): fixed-width text fields, "id=" last, the values after it.
*/

#include <irx.h>
#include <loadcore.h>
#include <thbase.h>
#include <intrman.h>
#include <sifman.h>
#include <sysclib.h>
#include <stdio.h>
#include "smap.h"
#include "ministack_udp.h"
#include "ra_snap.h"

IRX_ID("raagent", 1, 0);

#define CLIENT_PORT 18194
#define AGENT_PORT  18195
#define PAYLOAD     1472 // 1500 MTU - IP - UDP

typedef struct {
    eth_header_t eth;
    ip_header_t ip;
    udp_header_t udp;
    uint8_t payload[PAYLOAD];
} __attribute__((packed, aligned(4))) ra_pkt_t;

static ra_pkt_t pkt;
static udp_socket_t *sock;
static uint32_t srv_ip, mbox;
static char me[16] = "0.0.0.0";
static volatile int answered;
static uint32_t rx, sent, torn;
static uint8_t snap[RA_SNAP_TOTAL] __attribute__((aligned(64))); // ee_core DMAs here
static uint8_t stage[RA_SNAP_MAX_BYTES];

static uint32_t parse_ip(const char *s)
{
    uint32_t ip = 0, part = 0;
    for (; *s; s++)
        if (*s == '.')
            ip = ip << 8 | part, part = 0;
        else
            part = part * 10 + (*s - '0');
    return ip << 8 | part;
}

static int rx_handler(udp_socket_t *s, void *arg, const uint8_t *hdr, uint16_t hdr_len) // smap receive context
{
    uint32_t w[2];
    (void)s, (void)arg, (void)hdr, (void)hdr_len;
    // the payload starts at 42 (ETH + IP + UDP), but the SMAP RX FIFO is read in 32-bit words: from 42 the console
    // returned the bytes from 40 (PCSX2 honoured 42), so RAO1 never matched there. Read from 40, the tag at byte 2
    smap_fifo_read(40, w, 8);
    rx++;
    if (!strncmp((const char *)w + 2, "RAO1", 4))
        answered = 1;
    return 0;
}

static void send_text(const char *t)
{
    int n = strlen(t);
    memcpy(pkt.payload, t, n);
    udp_packet_send(sock, (udp_packet_t *)&pkt, n);
}

static void send_snapshot(const volatile struct ra_snap *s, uint32_t nb)
{
    static uint32_t seq;
    char id[16];
    int i, h;
    for (i = 0; i < 15; i++) { // the serial padded with '~' (serials hold '_')
        char c = i < 16 ? s->game_id[i] : 0;
        id[i] = c >= 0x20 && c < 0x7f ? c : '~';
    }
    id[15] = 0;
    h = sprintf((char *)pkt.payload,
                "RA15 seq=%06u sz=%04u us=00000 mx=00000 rxq=000 fail=000000 err=+000 sk=000000 lk=0000 sq=%06u "
                "ds=%06u bad=%04u n=%04u vb=%04u pt=0 np=1 rc=%07u fc=%07u id=%s ",
                seq++ % 1000000, 0, s->seq % 1000000, s->dma_skip % 1000000, torn % 10000, s->count % 10000, nb,
                s->read_cycles > 9999999 ? 9999999 : s->read_cycles,
                s->frame_cycles > 9999999 ? 9999999 : s->frame_cycles, id);
    // sz: the payload length, known once the header is written (same width, so the header does not move)
    char sz[5];
    sprintf(sz, "%04u", (unsigned)(h + nb) % 10000);
    memcpy(pkt.payload + 19, sz, 4); // "RA15 seq=000000 sz=" is 19 characters
    memcpy(pkt.payload + h, stage, nb);
    udp_packet_send(sock, (udp_packet_t *)&pkt, h + nb);
    sent++;
}

static void agent_thread(void *arg)
{
    const volatile struct ra_snap *s = (const volatile struct ra_snap *)snap;
    uint32_t last = 0, tick = 0;
    char t[64];
    (void)arg;
    udp_packet_init((udp_packet_t *)&pkt, srv_ip, CLIENT_PORT); // ARP for the client's MAC: a thread may wait
    if (mbox != 0) { // tell ee_core where to DMA the snapshots
        static uint32_t word[4] __attribute__((aligned(16)));
        SifDmaTransfer_t d;
        int st;
        word[0] = (uint32_t)snap;
        d.src = word, d.dest = (void *)mbox, d.size = 16, d.attr = 0;
        CpuSuspendIntr(&st);
        sceSifSetDma(&d, 1);
        CpuResumeIntr(st);
    }
    for (;; DelayThread(4000), tick++) { // 4 ms: catches every 16.7 ms snapshot once (raudp's RA_POLL_US)
        if (!answered) {
            if (tick % 250 == 0) // once a second until the client answers
                sprintf(t, "RAP1 %s %d", me, AGENT_PORT), send_text(t);
            continue;
        }
        if (tick % 2500 == 0) // ten seconds
            // the badge fields (unused, PROTOCOL.md) carry the agent's view, logged by the client: the snapshot
            // sequence in its buffer, the buffer's magic, the EE mailbox (0 = the loader placed no watch block)
            sprintf(t, "RAH1 %u %u %06u %08x %x", (unsigned)rx, 0u, (unsigned)(s->seq % 1000000), (unsigned)s->magic,
                    (unsigned)mbox), send_text(t);
        if (s->magic != RA_SNAP_MAGIC || s->seq == last)
            continue;
        uint32_t sq = s->seq, nb = s->bytes;
        if (nb > PAYLOAD - 256 || nb > RA_SNAP_MAX_BYTES) // one packet: the header is under 256 bytes
            continue;
        memcpy(stage, snap + RA_SNAP_HDR, nb);
        if (*(volatile uint32_t *)(snap + RA_SNAP_TRAILER_OFF(nb)) != sq || s->seq != sq) { // overwritten mid-copy
            torn++;
            continue;
        }
        send_snapshot(s, nb);
        last = sq;
    }
}

int _start(int argc, char *argv[])
{
    iop_thread_t th;
    int i;
    for (i = 1; i < argc; i++)
        if (!strncmp(argv[i], "srv=", 4))
            srv_ip = parse_ip(argv[i] + 4);
        else if (!strncmp(argv[i], "me=", 3))
            strncpy(me, argv[i] + 3, sizeof(me) - 1);
        else if (!strncmp(argv[i], "mbox=", 5))
            mbox = strtoul(argv[i] + 5, NULL, 16);
    printf("raagent: client %08x, me %s, mailbox %08x, buffer %p\n", (unsigned)srv_ip, me, (unsigned)mbox, snap);
    if (srv_ip == 0 || (sock = udp_bind(AGENT_PORT, rx_handler, NULL)) == NULL)
        return MODULE_NO_RESIDENT_END;
    th.attr = TH_C;
    th.thread = agent_thread;
    th.priority = 0x50;
    th.stacksize = 0x800;
    th.option = 0;
    int tid = CreateThread(&th);
    if (tid < 0 || StartThread(tid, NULL) < 0)
        return MODULE_NO_RESIDENT_END;
    return MODULE_RESIDENT_END;
}
