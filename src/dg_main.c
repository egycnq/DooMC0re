#include "platform.h"
#include "libc_stubs.h"
#include "usb.h"
#include "savedata_probe.h"
#include "dg_save.h"
#include "dg_wadmenu.h"
#if UDP_PROBE
#include "dg_udpprobe.h"
#endif

#define STATUS_EVERY_FRAMES 300
#define AUDIO_FRAMES_PER_MS 48

extern int gametic;
extern int gameepisode;
extern int gamemap;
extern int gamestate;
int  Z_FreeMemory(void);
void Z_CheckHeap(void);

int  DG_AudioQueueMinFrames(void);

#if MULTIPLAYER
int  NET_CL_AverageLatency(void);
#endif
extern char i_error_msg[256];

extern void doomgeneric_Create(int argc, char **argv);
extern void doomgeneric_Tick(void);

struct platform g_plt;

int dbg_trace_on = 0;

void dbg_trace(const char *tag)
{
    if (!dbg_trace_on) return;
    plt_log(&g_plt, tag);
}

extern jmp_buf exit_jmp;
extern int exit_jmp_set;

static void show_loading(struct platform *plt, const char *msg)
{
    dg_wad_splash(plt, msg);
    plt_log(plt, msg);
}

__attribute__((section(".text._start")))
void _start(void *eboot, void *dlsym, struct ext_args *ext)
{
    u8 *plt_bytes = (u8 *)&g_plt;
    for (u32 i = 0; i < sizeof(struct platform); i++) plt_bytes[i] = 0;

    if (plt_init(&g_plt, eboot, dlsym, ext) != 0)
    {
        plt_log(&g_plt, "plt_init failed, nothing to draw on\n");
        return;
    }

    libc_init(g_plt.gadget, g_plt.dlsym_fn, g_plt.mmap,
              g_plt.kopen, g_plt.kread, g_plt.kwrite, g_plt.kclose,
              g_plt.klseek, g_plt.kunlink, g_plt.usleep,
              g_plt.kmkdir,
              g_plt.sendto, g_plt.log_fd, g_plt.log_sa);

    plt_savedata_check(&g_plt, NULL);

#if SAVEDATA_PROBE
    plt_savedata_probe(&g_plt);
#endif

    show_loading(&g_plt, "STARTING UP");

#if UDP_PROBE
    dg_udp_probe(&g_plt);
#endif

    dg_wad_splash(&g_plt, "READING USB DEVICE");

    struct wad_entry wads[MAX_WADS];
    int wad_count = usb_load_wads(g_plt.gadget, g_plt.dlsym_fn,
                                  g_plt.load_mod, g_plt.mmap,
                                  g_plt.kopen, g_plt.kwrite, g_plt.kclose,
                                  g_plt.kunlink, g_plt.kmkdir, g_plt.getdents,
                                  g_plt.sendto, g_plt.log_fd, g_plt.log_sa,
                                  wads, MAX_WADS);
    if (wad_count > 0) plt_log(&g_plt, "USB WADs loaded\n");

    const char *wad_path = dg_wad_menu(&g_plt);
    if (!wad_path)
    {
        plt_log(&g_plt, "No WAD file found!\n");
        plt_log(&g_plt, "Put a .wad in /doom/ on USB or in /savedata0/\n");
        plt_sleep_ms(&g_plt, 5000);
        plt_shutdown(&g_plt);
        return;
    }

    plt_log(&g_plt, "WAD: ");
    plt_log(&g_plt, wad_path);
    plt_log(&g_plt, "\n");

    static char *doom_argv[12];
    const char *splash = "LOADING DOOM";
    int argc = 0;

    doom_argv[argc++] = (char *)"doom";
    doom_argv[argc++] = (char *)"-iwad";
    doom_argv[argc++] = (char *)wad_path;

#if MULTIPLAYER
    static struct dg_net_choice net_choice;
    static char nodes_str[4], skill_str[4];

    net_choice.mode = DG_NETMODE_HOST;
    net_choice.players = 2;
    net_choice.game_type = 1;
    net_choice.skill = 3;

    net_choice.octet[0] = 192;
    net_choice.octet[1] = 168;
    net_choice.octet[2] = 1;
    net_choice.octet[3] = 100;

    /* an address preset by doom_launcher.py --connect wins over the default */
    if (g_plt.join_ip)
    {
        for (int i = 0; i < 4; i++)
            net_choice.octet[i] = (g_plt.join_ip >> (i * 8)) & 0xFF;
        net_choice.port = g_plt.join_port;
        net_choice.mode = DG_NETMODE_JOIN;
    }

    if (dg_net_offer(&g_plt) && dg_net_menu(&g_plt, &net_choice))
    {
        if (net_choice.mode == DG_NETMODE_HOST)
        {
            doom_argv[argc++] = (char *)"-privateserver";
            splash = "WAITING FOR PLAYERS";
        }
        else
        {
            doom_argv[argc++] = (char *)"-connect";
            doom_argv[argc++] = net_choice.addr;
            splash = "CONNECTING";
        }

        snprintf(nodes_str, sizeof(nodes_str), "%d", net_choice.players);
        snprintf(skill_str, sizeof(skill_str), "%d", net_choice.skill);
        doom_argv[argc++] = (char *)"-nodes";
        doom_argv[argc++] = nodes_str;
        doom_argv[argc++] = (char *)"-skill";
        doom_argv[argc++] = skill_str;
        if (net_choice.game_type == 1)
            doom_argv[argc++] = (char *)"-deathmatch";
        if (net_choice.game_type == 2)
            doom_argv[argc++] = (char *)"-altdeath";

        plt_log(&g_plt, "netgame requested\n");
    }
#endif

    doom_argv[argc] = NULL;

    dg_wad_splash(&g_plt, splash);

    if (setjmp(exit_jmp) == 0)
    {
        /* without this every I_Error hangs the console on its last frame */
        exit_jmp_set = 1;
        plt_log(&g_plt, "Starting DOOM engine\n");
        doomgeneric_Create(argc, doom_argv);
        plt_log(&g_plt, "Create done\n");

        plt_blank(&g_plt);

        int last_gamestate = -1;
        unsigned last_beat_ms = plt_get_ms(&g_plt);
        unsigned last_beat_frames = 0;
        u64 last_beat_audio_us = g_plt.audio_block_us;
        u64 last_beat_vsync_us = g_plt.vsync_block_us;

        for (;;)
        {
            doomgeneric_Tick();

            unsigned frame_count = g_plt.total_frames;

#if DOOM_TRACE
            int verbose = (frame_count > 420 && frame_count < 760);
            dbg_trace_on = (frame_count > 500 && frame_count < 660);
            if (frame_count % 30 == 0) Z_CheckHeap();
#else
            const int verbose = 0;
#endif

            if (frame_count % STATUS_EVERY_FRAMES == 0 || verbose ||
                (int)gamestate != last_gamestate)
            {
                unsigned now_ms       = plt_get_ms(&g_plt);
                unsigned delta_ms     = now_ms - last_beat_ms;
                unsigned delta_frames = frame_count - last_beat_frames;

                char netinfo[24];
                netinfo[0] = 0;
#if MULTIPLAYER
                int latency_ms = NET_CL_AverageLatency();
                if (latency_ms > 0)
                    snprintf(netinfo, sizeof(netinfo), " lat=%dms", latency_ms);
#endif

                last_gamestate = (int)gamestate;
                printf("F=%u gs=%d tic=%d ep=%d map=%d zfree=%d "
                       "fps=%u.%u aud=%ums vsync=%ums qmin=%dms%s\n",
                       frame_count, (int)gamestate, gametic, gameepisode,
                       gamemap, Z_FreeMemory(),
                       delta_ms ? (delta_frames * 1000) / delta_ms : 0,
                       delta_ms ? ((delta_frames * 10000) / delta_ms) % 10 : 0,
                       (unsigned)((g_plt.audio_block_us -
                                   last_beat_audio_us) / 1000),
                       (unsigned)((g_plt.vsync_block_us -
                                   last_beat_vsync_us) / 1000),
                       DG_AudioQueueMinFrames() / AUDIO_FRAMES_PER_MS, netinfo);

                last_beat_ms       = now_ms;
                last_beat_frames   = frame_count;
                last_beat_audio_us = g_plt.audio_block_us;
                last_beat_vsync_us = g_plt.vsync_block_us;
            }
        }
    }
    plt_log(&g_plt, "DOOM exited\n");
    exit_jmp_set = 0;

    if (i_error_msg[0])
        dg_wad_error(&g_plt, i_error_msg);

    plt_shutdown(&g_plt);
}
