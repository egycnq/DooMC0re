#include "dg_udpprobe.h"
#include "dg_wadmenu.h"
#include "libc_stubs.h"

#define POLLIN_    0x0001
#define DS_CIRCLE  0x00002000u

#define SOCKADDR_LEN      16
#define SOCKADDR_PORT_OFF 2
#define SOCKADDR_IP_OFF   4

#define PROBE_BUF_SIZE       1500
#define PROBE_UNAVAIL_FRAMES 240

static u8 probe_buf[PROBE_BUF_SIZE];

static int sock_readable(struct platform *plt, s32 fd)
{
    u8 pollfd[8];

    if (!plt->poll) return 0;
    *(s32 *)(pollfd + 0) = fd;
    *(u16 *)(pollfd + 4) = POLLIN_;
    *(u16 *)(pollfd + 6) = 0;
    return (s32)NC(plt->gadget, plt->poll, (u64)pollfd, 1, 0, 0, 0, 0) > 0;
}

void dg_udp_probe(struct platform *plt)
{
    s32 fd = plt->net_fd;
    int received = 0, echoed = 0;
    u32 held = 0;
    int first_read = 1;

    dg_status_screen_reset();

    if (fd < 0)
    {
        printf("[UDP] launcher gave no socket -- inbound UDP untestable\n");
        for (int i = 0; i < PROBE_UNAVAIL_FRAMES; i++)
            dg_status_screen(plt, "UDP PROBE", "NO SOCKET FROM LAUNCHER");
        return;
    }

    printf("[UDP] probe listening on fd %d, port %d\n", fd, NET_PROBE_PORT);

    for (;;)
    {
        char line1[64], line2[64];

        if (sock_readable(plt, fd))
        {
            u8  sockaddr[SOCKADDR_LEN];
            u32 sockaddr_len = SOCKADDR_LEN;
            s32 rx_len;

            for (int i = 0; i < SOCKADDR_LEN; i++) sockaddr[i] = 0;
            rx_len = (s32)NC(plt->gadget, plt->recvfrom, (u64)fd,
                             (u64)probe_buf, sizeof(probe_buf), 0,
                             (u64)sockaddr, (u64)&sockaddr_len);

            if (rx_len > 0)
            {
                u32 ip = *(u32 *)(sockaddr + SOCKADDR_IP_OFF);
                u16 port_be = *(u16 *)(sockaddr + SOCKADDR_PORT_OFF);
                received++;

                printf("[UDP] rx %d bytes from %d.%d.%d.%d:%d\n", rx_len,
                       ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
                       (ip >> 24) & 0xFF,
                       (port_be >> 8) | ((port_be & 0xFF) << 8));

                if ((s32)NC(plt->gadget, plt->sendto, (u64)fd,
                            (u64)probe_buf, (u64)rx_len, 0,
                            (u64)sockaddr, SOCKADDR_LEN) == rx_len)
                {
                    echoed++;
                }
                else
                {
                    printf("[UDP] echo FAILED\n");
                }
            }
        }

        snprintf(line1, sizeof(line1), "UDP PROBE ON PORT %d", NET_PROBE_PORT);
        snprintf(line2, sizeof(line2), "%d IN   %d ECHOED", received, echoed);
        dg_status_screen(plt, line1, line2);

        u32 buttons = plt_pad_read(plt);
        if (buttons == PLT_PAD_NONE) buttons = held;
        if (first_read)
        {
            held = buttons;
            first_read = 0;
        }
        if ((buttons & ~held) & DS_CIRCLE) break;
        held = buttons;
    }

    printf("[UDP] probe done: %d received, %d echoed\n", received, echoed);
}
