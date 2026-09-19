#include "platform.h"
#include "libc_stubs.h"

#include "doomgeneric/doomgeneric.h"
#include "doomgeneric/doomkeys.h"

extern struct platform g_plt;

#define MAX_KEYS 64

static struct
{
    unsigned char key;
    unsigned char pressed;
} key_queue[MAX_KEYS];
static int key_head = 0, key_tail = 0;

static void push_key(unsigned char key, int pressed)
{
    int next = (key_head + 1) % MAX_KEYS;
    if (next == key_tail) return;
    key_queue[key_head].key = key;
    key_queue[key_head].pressed = pressed;
    key_head = next;
}

#define KEY_CODES 256

static unsigned char key_held[KEY_CODES];

#define DS_CROSS     0x00004000
#define DS_CIRCLE    0x00002000
#define DS_SQUARE    0x00008000
#define DS_TRIANGLE  0x00001000
#define DS_L1        0x00000400
#define DS_R1        0x00000800
#define DS_L2        0x00000100
#define DS_R2        0x00000200
#define DS_UP        0x00000010
#define DS_DOWN      0x00000040
#define DS_LEFT      0x00000080
#define DS_RIGHT     0x00000020
#define DS_OPTIONS   0x00000008
#define DS_L3        0x00000002
#define DS_R3        0x00000004

struct btn_map
{
    u32 mask;
    unsigned char in_game, in_sub, in_root;
};

static const struct btn_map button_map[] = {
    { DS_CROSS,    KEY_FIRE,       KEY_ENTER,      KEY_ENTER      },
    { DS_CIRCLE,   KEY_USE,        KEY_BACKSPACE,  KEY_ESCAPE     },
    { DS_SQUARE,   KEY_RALT,       0,              0              },
    { DS_TRIANGLE, KEY_TAB,        0,              0              },
    { DS_UP,       KEY_UPARROW,    KEY_UPARROW,    KEY_UPARROW    },
    { DS_DOWN,     KEY_DOWNARROW,  KEY_DOWNARROW,  KEY_DOWNARROW  },
    { DS_LEFT,     KEY_LEFTARROW,  KEY_LEFTARROW,  KEY_LEFTARROW  },
    { DS_RIGHT,    KEY_RIGHTARROW, KEY_RIGHTARROW, KEY_RIGHTARROW },
    { DS_L1,       ',',            0,              0              },
    { DS_R1,       '.',            0,              0              },
    { DS_L2,       KEY_RSHIFT,     0,              0              },
    { DS_R2,       KEY_RALT,       0,              0              },
    { DS_OPTIONS,  KEY_ESCAPE,     KEY_ESCAPE,     KEY_ESCAPE     },
    { DS_L3,       KEY_ENTER,      KEY_ENTER,      KEY_ENTER      },
    { DS_R3,       KEY_ENTER,      KEY_ENTER,      KEY_ENTER      },
    { 0, 0, 0, 0 }
};

/* one pad, four contexts, no keyboard */
static unsigned char key_for(const struct btn_map *button, int menu_state)
{
    switch (menu_state)
    {
    case DG_MENU_PROMPT:
        if (button->mask == DS_CROSS)  return (unsigned char)DG_MenuConfirmKey();
        if (button->mask == DS_CIRCLE) return (unsigned char)DG_MenuAbortKey();
        if (button->mask == DS_OPTIONS) return KEY_ESCAPE;
        return 0;
    case DG_MENU_ROOT: return button->in_root;
    case DG_MENU_SUB:  return button->in_sub;
    default:           return button->in_game;
    }
}

#define MAX_BTNS 32

static unsigned char btn_down[MAX_BTNS];
static unsigned char btn_latched[MAX_BTNS];

static void poll_pad(void)
{
    unsigned char held_now[KEY_CODES];
    int i, code;

    u32 buttons = plt_pad_read(&g_plt);
    if (buttons == PLT_PAD_NONE) return;

    int menu_state = DG_MenuState();

    for (code = 0; code < KEY_CODES; code++) held_now[code] = 0;

    for (i = 0; i < MAX_BTNS && button_map[i].mask; i++)
    {
        if (buttons & button_map[i].mask)
        {
            if (!btn_down[i])
            {
                btn_down[i] = 1;
                btn_latched[i] = key_for(&button_map[i], menu_state);
            }
            if (btn_latched[i]) held_now[btn_latched[i]] = 1;
        }
        else
        {
            btn_down[i] = 0;
            btn_latched[i] = 0;
        }
    }

    for (code = 0; code < KEY_CODES; code++)
    {
        if (held_now[code] && !key_held[code])
            push_key((unsigned char)code, 1);
        else if (!held_now[code] && key_held[code])
            push_key((unsigned char)code, 0);
    }
    for (code = 0; code < KEY_CODES; code++) key_held[code] = held_now[code];
}

#define STICK_DEADZONE 24

static int stick_scale(int axis_value)
{
    int scaled;
    if (axis_value > STICK_DEADZONE)
        scaled = ((axis_value - STICK_DEADZONE) * 128) / (127 - STICK_DEADZONE);
    else if (axis_value < -STICK_DEADZONE)
        scaled = ((axis_value + STICK_DEADZONE) * 128) / (128 - STICK_DEADZONE);
    else
        return 0;

    if (scaled > 128)  scaled = 128;
    if (scaled < -128) scaled = -128;
    return scaled;
}

void DG_GetAnalog(int *lx, int *ly, int *rx, int *ry)
{
    *lx = stick_scale(g_plt.stick_lx);
    *ly = stick_scale(g_plt.stick_ly);
    *rx = stick_scale(g_plt.stick_rx);
    *ry = stick_scale(g_plt.stick_ry);
}

void DG_Init(void)
{
}

void DG_DrawFrame(void)
{
    plt_video_flip(&g_plt, DG_ScreenBuffer);
}

void DG_SleepMs(uint32_t ms)
{
    plt_sleep_ms(&g_plt, ms);
}

uint32_t DG_GetTicksMs(void)
{
    return plt_get_ms(&g_plt);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    static u32 last_poll_frame = 0xFFFFFFFFu;
    if (g_plt.total_frames != last_poll_frame)
    {
        last_poll_frame = g_plt.total_frames;
        poll_pad();
    }

    if (key_tail == key_head) return 0;

    *pressed = key_queue[key_tail].pressed;
    *key     = key_queue[key_tail].key;
    key_tail = (key_tail + 1) % MAX_KEYS;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}


int plt_audio_grain(void)
{
    return (int)g_plt.audio_grain;
}

int plt_audio_thread_start(void *(*entry)(void *))
{
    return plt_thread_start(&g_plt, entry, 0, "doom_audio");
}


#define DEFAULT_NET_PORT  2342
#define SOCKADDR_LEN      16
#define SOCKADDR_PORT_OFF 2
#define SOCKADDR_IP_OFF   4
#define AF_INET_          2
#define POLLIN_           0x0001

/* The launcher binds the socket, so it owns the port number too */
int plt_net_port(void)
{
    return g_plt.net_port ? g_plt.net_port : DEFAULT_NET_PORT;
}

int plt_net_ready(void)
{
    return g_plt.net_fd >= 0;
}

int plt_net_local_ip(char *buf, int max_len)
{
    u32 ip = g_plt.local_ip;

    if (!ip) return snprintf(buf, max_len, "UNKNOWN");

    return snprintf(buf, max_len, "%d.%d.%d.%d",
                    ip & 0xFF, (ip >> 8) & 0xFF,
                    (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
}

int plt_net_send(const void *buf, int len, u32 ip, u16 port)
{
    u8 sockaddr[SOCKADDR_LEN];

    if (g_plt.net_fd < 0 || !g_plt.sendto) return -1;

    for (int i = 0; i < SOCKADDR_LEN; i++) sockaddr[i] = 0;
    sockaddr[0] = SOCKADDR_LEN;
    sockaddr[1] = AF_INET_;
    sockaddr[SOCKADDR_PORT_OFF]     = (u8)(port >> 8);
    sockaddr[SOCKADDR_PORT_OFF + 1] = (u8)port;
    *(u32 *)(sockaddr + SOCKADDR_IP_OFF) = ip;

    return (s32)NC(g_plt.gadget, g_plt.sendto, (u64)g_plt.net_fd, (u64)buf,
                   (u64)len, 0, (u64)sockaddr, SOCKADDR_LEN);
}

int plt_net_recv(void *buf, int max_len, u32 *ip, u16 *port)
{
    u8 sockaddr[SOCKADDR_LEN], pollfd[8];
    u32 sockaddr_len;
    s32 received;

    if (g_plt.net_fd < 0 || !g_plt.recvfrom || !g_plt.poll) return 0;

    *(s32 *)(pollfd + 0) = g_plt.net_fd;
    *(u16 *)(pollfd + 4) = POLLIN_;
    *(u16 *)(pollfd + 6) = 0;

    for (int tries = 0; tries < 8; tries++)
    {
        if ((s32)NC(g_plt.gadget, g_plt.poll, (u64)pollfd,
                    1, 0, 0, 0, 0) <= 0) return 0;

        sockaddr_len = SOCKADDR_LEN;
        for (int i = 0; i < SOCKADDR_LEN; i++) sockaddr[i] = 0;
        received = (s32)NC(g_plt.gadget, g_plt.recvfrom, (u64)g_plt.net_fd,
                           (u64)buf, (u64)max_len, 0, (u64)sockaddr,
                           (u64)&sockaddr_len);
        if (received < 0) return 0;
        if (received > 0)
        {
            *ip   = *(u32 *)(sockaddr + SOCKADDR_IP_OFF);
            *port = (u16)((sockaddr[SOCKADDR_PORT_OFF] << 8) |
                          sockaddr[SOCKADDR_PORT_OFF + 1]);
            return received;
        }
    }

    return 0;
}

int plt_audio_out(void *buf)
{
    if (g_plt.audio_handle < 0 || !g_plt.aud_out) return 0;

    u64 start_us = g_plt.get_proc_time
                   ? NC(g_plt.gadget, g_plt.get_proc_time, 0,0,0,0,0,0) : 0;
    NC(g_plt.gadget, g_plt.aud_out, (u64)g_plt.audio_handle, (u64)buf, 0, 0, 0, 0);
    if (start_us)
        g_plt.audio_block_us +=
            NC(g_plt.gadget, g_plt.get_proc_time, 0,0,0,0,0,0) - start_us;

    return 1;
}
