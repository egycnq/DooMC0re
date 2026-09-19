//
// PS5 UDP transport for the netgame, replacing net_sdl.c.
//
// The socket itself is bound by the Lua launcher and reached through
// plt_net_send/plt_net_recv in dg_platform.c -- socket() is blocked inside the
// payload from firmware 8.00 on, and this file cannot include platform.h
// anyway because its FILE collides with the one the DOOM headers pull in.
//
// Address handling follows net_sdl.c: net_addr_t handles must stay valid for
// as long as the net code holds them, so they live in a table rather than on
// the stack.
//

#include <stdio.h>
#include <string.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_misc.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_packet.h"
#include "net_ps5.h"
#include "z_zone.h"

extern int plt_net_ready(void);
extern int plt_net_port(void);
extern int plt_net_send(const void *buf, int len, unsigned int ip,
                        unsigned short port);
extern int plt_net_recv(void *buf, int max, unsigned int *ip,
                        unsigned short *port);

// 1500 is what net_sdl.c reads into and nothing here has been seen to exceed
// it; 2048 is margin, because a long resend batch is still a single datagram
// and recvfrom would truncate it without saying so.
#define RECV_MAX     2048

typedef struct
{
    unsigned int   ip;      // network order, as it came off the wire
    unsigned short port;    // host order
} ps5_addr_t;

typedef struct
{
    ps5_addr_t addr;
    net_addr_t net_addr;
} addrpair_t;

static addrpair_t **addr_table;
static int addr_table_size = -1;
static boolean initted = false;
static byte recvbuf[RECV_MAX];

// Z_Malloc does not zero, and the search below reads a NULL slot as free.
static addrpair_t **AllocAddrTable(int size)
{
    addrpair_t **table;

    table = Z_Malloc(sizeof(addrpair_t *) * size, PU_STATIC, 0);
    memset(table, 0, sizeof(addrpair_t *) * size);

    return table;
}

static void NET_PS5_InitAddrTable(void)
{
    addr_table_size = 16;
    addr_table = AllocAddrTable(addr_table_size);
}

// Returns the existing handle for this address, or adds it. The net code
// compares net_addr_t pointers, so the same peer must always map to the same
// entry.
static net_addr_t *NET_PS5_FindAddress(unsigned int ip, unsigned short port)
{
    addrpair_t *new_entry;
    int empty_entry = -1;
    int i;

    if (addr_table_size < 0)
    {
        NET_PS5_InitAddrTable();
    }

    for (i = 0; i < addr_table_size; ++i)
    {
        if (addr_table[i] != NULL
         && addr_table[i]->addr.ip == ip
         && addr_table[i]->addr.port == port)
        {
            return &addr_table[i]->net_addr;
        }

        if (empty_entry < 0 && addr_table[i] == NULL)
        {
            empty_entry = i;
        }
    }

    if (empty_entry < 0)
    {
        addrpair_t **bigger;
        int newsize = addr_table_size * 2;

        empty_entry = addr_table_size;
        bigger = AllocAddrTable(newsize);
        memcpy(bigger, addr_table, sizeof(addrpair_t *) * addr_table_size);
        Z_Free(addr_table);
        addr_table = bigger;
        addr_table_size = newsize;
    }

    new_entry = Z_Malloc(sizeof(addrpair_t), PU_STATIC, 0);
    new_entry->addr.ip = ip;
    new_entry->addr.port = port;
    new_entry->net_addr.handle = &new_entry->addr;
    new_entry->net_addr.module = &net_ps5_module;

    addr_table[empty_entry] = new_entry;

    return &new_entry->net_addr;
}

static void NET_PS5_FreeAddress(net_addr_t *addr)
{
    int i;

    for (i = 0; i < addr_table_size; ++i)
    {
        if (addr_table[i] != NULL && addr == &addr_table[i]->net_addr)
        {
            Z_Free(addr_table[i]);
            addr_table[i] = NULL;
            return;
        }
    }

    I_Error("NET_PS5_FreeAddress: Attempted to remove an unused address!");
}

// Client and server share the launcher's one socket, so there is nothing to
// open here and no -port to honour -- change NET_PORT in the launcher instead.
// A server on some other port is still reachable: that port comes from the
// -connect address.
static boolean InitSocket(const char *role)
{
    if (initted)
    {
        return true;
    }

    if (!plt_net_ready())
    {
        printf("NET_PS5: %s: launcher provided no UDP socket\n", role);
        return false;
    }

    printf("NET_PS5: %s ready on port %d\n", role, plt_net_port());
    initted = true;

    return true;
}

static boolean NET_PS5_InitClient(void)
{
    return InitSocket("client");
}

static boolean NET_PS5_InitServer(void)
{
    return InitSocket("server");
}

static void NET_PS5_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    unsigned int ip;
    unsigned short port;

    if (addr == &net_broadcast_addr)
    {
        // Only LAN discovery sends here, and it is not needed for a direct
        // connection. Left in so net_query.c behaves; the sandbox may drop it.
        ip = 0xFFFFFFFF;
        port = plt_net_port();
    }
    else
    {
        ps5_addr_t *a = (ps5_addr_t *) addr->handle;
        ip = a->ip;
        port = a->port;
    }

    plt_net_send(packet->data, packet->len, ip, port);
}

static boolean NET_PS5_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    unsigned int ip = 0;
    unsigned short port = 0;
    int len;

    len = plt_net_recv(recvbuf, RECV_MAX, &ip, &port);

    if (len <= 0)
    {
        return false;
    }

    *packet = NET_NewPacket(len);
    memcpy((*packet)->data, recvbuf, len);
    (*packet)->len = len;

    *addr = NET_PS5_FindAddress(ip, port);

    return true;
}

static void NET_PS5_AddrToString(net_addr_t *addr, char *buffer,
                                 int buffer_len)
{
    ps5_addr_t *a = (ps5_addr_t *) addr->handle;
    unsigned int ip = a->ip;

    M_snprintf(buffer, buffer_len, "%d.%d.%d.%d:%d",
               ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
               (ip >> 24) & 0xFF, a->port);
}

// "1.2.3.4" or "1.2.3.4:2342". No DNS: there is no resolver in this build, and
// a netgame here is always started from an address the launcher was given.
static net_addr_t *NET_PS5_ResolveAddress(char *address)
{
    unsigned int b[4] = { 0, 0, 0, 0 };
    unsigned int port = plt_net_port();
    const char *s = address;
    unsigned int ip;
    int i;

    if (address == NULL)
    {
        return NULL;
    }

    for (i = 0; i < 4; ++i)
    {
        if (*s < '0' || *s > '9')
        {
            return NULL;
        }
        while (*s >= '0' && *s <= '9')
        {
            b[i] = b[i] * 10 + (*s++ - '0');
        }
        if (b[i] > 255)
        {
            return NULL;
        }
        if (i < 3)
        {
            if (*s != '.')
            {
                return NULL;
            }
            ++s;
        }
    }

    if (*s == ':')
    {
        const char *digits = ++s;

        port = 0;
        while (*s >= '0' && *s <= '9')
        {
            port = port * 10 + (*s++ - '0');
        }

        // A bare colon used to give port 0, and anything over 65535 wrapped
        // silently -- ":65538" quietly became port 2.
        if (s == digits || port > 65535)
        {
            return NULL;
        }
    }

    if (*s != '\0')
    {
        return NULL;
    }

    // Kept in the byte order recvfrom reports, so an address parsed from text
    // and one seen on the wire compare equal.
    ip = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);

    return NET_PS5_FindAddress(ip, (unsigned short) port);
}

net_module_t net_ps5_module =
{
    NET_PS5_InitClient,
    NET_PS5_InitServer,
    NET_PS5_SendPacket,
    NET_PS5_RecvPacket,
    NET_PS5_AddrToString,
    NET_PS5_FreeAddress,
    NET_PS5_ResolveAddress,
};
