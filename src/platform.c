#include "platform.h"
#include "libc_stubs.h"

#define LOG_SOCKADDR_LEN  16
#define VSYNC_FALLBACK_US 16000
#define MAX_THREADS       4

void plt_log(struct platform *plt, const char *msg)
{
    if (plt->log_fd < 0 || !plt->log_sendto) return;
    int len = 0;
    while (msg[len]) len++;
    NC(plt->gadget, plt->log_sendto, (u64)plt->log_fd, (u64)msg, (u64)len, 0,
       (u64)plt->log_sa, LOG_SOCKADDR_LEN);
}

static void clear_fb(u32 *fb)
{
    for (int i = 0; i < SCR_W * SCR_H; i++) fb[i] = 0xFF000000;
}

int plt_init(struct platform *plt, void *eboot, void *dlsym,
             struct ext_args *ext)
{
    void *gadget = (void *)((u64)eboot + GADGET_OFFSET);
    void *dlsym_fn = dlsym;
    plt->gadget = gadget;
    plt->dlsym_fn = dlsym_fn;
    plt->eboot_base = (u64)eboot;

    plt->log_fd = ext->log_fd;
    for (int i = 0; i < LOG_SOCKADDR_LEN; i++)
        plt->log_sa[i] = ext->log_addr[i];

    plt->mmap     = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "mmap");
    plt->munmap   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "munmap");
    plt->usleep   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelUsleep");
    plt->kopen    = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelOpen");
    plt->kread    = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelRead");
    plt->kwrite   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelWrite");
    plt->kclose   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelClose");
    plt->klseek   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelLseek");
    plt->kunlink  = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelUnlink");
    plt->kmkdir   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelMkdir");
    plt->load_mod = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                        "sceKernelLoadStartModule");
    plt->getdents = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                        "sceKernelGetdents");
    if (!plt->getdents)
        plt->getdents = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "getdents");

    /* socket() is blocked in the payload from firmware 8.00; the launcher opens
       every socket the C side uses */
    plt->sendto    = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sendto");
    plt->recvfrom  = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "recvfrom");
    plt->accept    = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "accept");
    plt->poll      = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "poll");
    plt->close_fn  = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "close");
    plt->getsockname = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "getsockname");
    plt->recv_fn   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "recv");
    plt->send_fn   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "send");
    plt->log_sendto = plt->sendto;

    plt->ftp_srv_fd  = (s32)ext->handoff[4];
    plt->ftp_data_fd = (s32)ext->handoff[5];
    plt->net_fd      = (s32)ext->handoff[6];
    plt->local_ip    = (u32)ext->handoff[7];
    plt->join_ip     = (u32)ext->handoff[8];
    plt->join_port   = (u16)(ext->handoff[8] >> 32);
    plt->net_port    = (u16)ext->handoff[9];

    if (!plt->usleep || !plt->load_mod)
    {
        plt_log(plt, "FAIL: missing usleep or load_mod\n");
        return -1;
    }

    plt_log(plt, "DOOM PS5 Init\n");

    void *cancel_fn = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                          "scePthreadCancel");
    if (cancel_fn)
    {
        u64 gs_thread = *(u64 *)(plt->eboot_base + EBOOT_GS_THREAD);
        if (gs_thread) NC(gadget, cancel_fn, gs_thread, 0, 0, 0, 0, 0);
    }
    NC(gadget, plt->usleep, 300000, 0, 0, 0, 0, 0);

    s32 vid_mod = (s32)NC(gadget, plt->load_mod,
                          (u64)"libSceVideoOut.sprx", 0,0,0,0,0);
    plt->vid_open     = SYM(gadget, dlsym_fn, vid_mod, "sceVideoOutOpen");
    plt->vid_close    = SYM(gadget, dlsym_fn, vid_mod, "sceVideoOutClose");
    plt->vid_set_buf  = SYM(gadget, dlsym_fn, vid_mod,
                            "sceVideoOutSetBufferAttribute");
    plt->vid_flip     = SYM(gadget, dlsym_fn, vid_mod, "sceVideoOutSubmitFlip");
    plt->vid_register_bufs = SYM(gadget, dlsym_fn, vid_mod,
                                 "sceVideoOutRegisterBuffers");

    plt->alloc_dm  = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                         "sceKernelAllocateDirectMemory");
    plt->map_dm    = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                         "sceKernelMapDirectMemory");
    plt->dm_size   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                         "sceKernelGetDirectMemorySize");
    plt->create_eq = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                         "sceKernelCreateEqueue");
    plt->wait_eq   = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                         "sceKernelWaitEqueue");
    plt->delete_eq = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                         "sceKernelDeleteEqueue");

    /* sceVideoOutOpen only succeeds once the emulator's handle is closed */
    s32 emu_vid = *(s32 *)(plt->eboot_base + EBOOT_VIDOUT);
    if (plt->vid_close && emu_vid >= 0)
        NC(gadget, plt->vid_close, (u64)emu_vid, 0, 0, 0, 0, 0);
    NC(gadget, plt->usleep, 100000, 0, 0, 0, 0, 0);

    plt->video_handle = (s32)NC(gadget, plt->vid_open, 0xFF, 0, 0, 0, 0, 0);

    u64 mem_total = plt->dm_size ? NC(gadget, plt->dm_size, 0,0,0,0,0,0)
                                 : 0x300000000ULL;
    u64 phys = 0;
    NC(gadget, plt->alloc_dm, 0, mem_total, FB_TOTAL, 0x200000, 3, (u64)&phys);
    void *vmem = 0;
    NC(gadget, plt->map_dm, (u64)&vmem, FB_TOTAL, 0x33, 0, phys, 0x200000);
    if (!vmem)
    {
        plt_log(plt, "FAIL: map_dm returned NULL\n");
        return -1;
    }
    plt->fbs[0] = (u8 *)vmem;
    plt->fbs[1] = (u8 *)vmem + FB_ALIGNED;
    clear_fb((u32 *)plt->fbs[0]);
    clear_fb((u32 *)plt->fbs[1]);

    u8 attr[64];
    for (int i = 0; i < 64; i++) attr[i] = 0;
    *(u32*)(attr + 0)  = 0x80000000;
    *(u32*)(attr + 4)  = 1;
    *(u32*)(attr + 12) = SCR_W;
    *(u32*)(attr + 16) = SCR_H;
    *(u32*)(attr + 20) = SCR_W;

    void *reg_fbs[2];
    reg_fbs[0] = plt->fbs[0];
    reg_fbs[1] = plt->fbs[1];
    if (NC(gadget, plt->vid_register_bufs, (u64)plt->video_handle, 0,
           (u64)reg_fbs, 2, (u64)attr, 0) != 0)
    {
        plt_log(plt, "FAIL: RegisterBuffers\n");
        return -1;
    }

    void *vid_rate = SYM(gadget, dlsym_fn, vid_mod, "sceVideoOutSetFlipRate");
    void *vid_evt  = SYM(gadget, dlsym_fn, vid_mod, "sceVideoOutAddFlipEvent");
    if (vid_rate) NC(gadget, vid_rate, (u64)plt->video_handle, 0, 0, 0, 0, 0);

    plt->eq = 0;
    if (plt->create_eq)
        NC(gadget, plt->create_eq, (u64)&plt->eq, (u64)"doomEQ", 0, 0, 0, 0);
    if (vid_evt && plt->eq)
        NC(gadget, vid_evt, plt->eq, (u64)plt->video_handle, 0, 0, 0, 0);

    plt->active = 0;
    plt->total_frames = 0;
    plt_log(plt, "Video OK\n");

    s32 aud_mod = (s32)NC(gadget, plt->load_mod,
                          (u64)"libSceAudioOut.sprx", 0,0,0,0,0);
    plt->aud_open = SYM(gadget, dlsym_fn, aud_mod, "sceAudioOutOpen");
    plt->aud_out  = SYM(gadget, dlsym_fn, aud_mod, "sceAudioOutOutput");
    void *aud_close_fn = SYM(gadget, dlsym_fn, aud_mod, "sceAudioOutClose");

    if (aud_close_fn)
        for (int handle = 0; handle < 8; handle++)
            NC(gadget, aud_close_fn, (u64)handle, 0, 0, 0, 0, 0);

    /* The grain must exceed the frame period or the device empties between feeds
       and the submit blocks paying it back. Not every value is accepted. */
    static const u32 grains[] = { SAMPLES_PER_BUF, 512, 256, 0 };
    plt->audio_handle = -1;
    plt->audio_grain = 0;
    if (plt->aud_open)
    {
        for (int i = 0; grains[i]; i++)
        {
            plt->audio_handle = (s32)NC(gadget, plt->aud_open, 0xFF, 0, 0,
                                        grains[i], SAMPLE_RATE,
                                        AUDIO_S16_STEREO);
            if (plt->audio_handle >= 0)
            {
                plt->audio_grain = grains[i];
                break;
            }
        }
    }
    if (plt->audio_grain != SAMPLES_PER_BUF)
        plt_log(plt, "audio: fell back to a smaller grain\n");

    NC(gadget, plt->load_mod, (u64)"libSceUserService.sprx", 0, 0, 0, 0, 0);
    s32 pad_mod = (s32)NC(gadget, plt->load_mod,
                          (u64)"libScePad.sprx", 0,0,0,0,0);
    plt->pad_init = SYM(gadget, dlsym_fn, pad_mod, "scePadInit");
    plt->pad_open = SYM(gadget, dlsym_fn, pad_mod, "scePadGetHandle");
    plt->pad_read = SYM(gadget, dlsym_fn, pad_mod, "scePadRead");
    plt->pad_handle = -1;
    if (plt->pad_init) NC(gadget, plt->pad_init, 0, 0, 0, 0, 0, 0);
    plt->user_id = (s32)ext->handoff[3];
    if (plt->pad_open && plt->user_id)
        plt->pad_handle = (s32)NC(gadget, plt->pad_open, (u64)plt->user_id,
                                  0, 0, 0, 0, 0);
    plt_log(plt, plt->pad_handle >= 0 ? "Pad OK\n" : "Pad N/A\n");

    plt->thread_create = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                             "scePthreadCreate");

    plt->get_proc_time = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE,
                             "sceKernelGetProcessTime");
    if (plt->get_proc_time)
        plt->start_time_us = NC(gadget, plt->get_proc_time, 0,0,0,0,0,0);
    else
        plt->start_time_us = 0;
    plt->total_frames = 0;

    plt->kstat = SYM(gadget, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelStat");
    /* The dlsym sceSaveDataMount export refuses every mount with 0x809F000F,
       read-only included; only the eboot GOT entry works. */
    plt->save_mount_got =
        (void *)*(u64 *)(plt->eboot_base + EBOOT_SAVEDATA_MOUNT_GOT);
    for (int i = 0; i < 32; i++) plt->save_dir[i] = 0;

    s32 save_mod = (s32)NC(gadget, plt->load_mod,
                           (u64)"libSceSaveData.sprx", 0,0,0,0,0);
    plt->save_mod = save_mod;
    if (save_mod >= 0)
    {
        plt->save_init  = SYM(gadget, dlsym_fn, save_mod,
                              "sceSaveDataInitialize3");
        plt->save_mount = SYM(gadget, dlsym_fn, save_mod, "sceSaveDataMount");
        plt->save_umount = SYM(gadget, dlsym_fn, save_mod, "sceSaveDataUmount");
        if (plt->save_init)
            NC(gadget, plt->save_init, 0, 0, 0, 0, 0, 0);
        plt_log(plt, "SaveData module OK\n");
    }
    else
    {
        plt_log(plt, "SaveData module N/A\n");
    }
    plt->savedata_ok = 0;

    return 0;
}

void plt_shutdown(struct platform *plt)
{
    void *gadget = plt->gadget;
    if (plt->eq && plt->delete_eq)
        NC(gadget, plt->delete_eq, plt->eq, 0,0,0,0,0);
    clear_fb((u32 *)plt->fbs[0]);
    clear_fb((u32 *)plt->fbs[1]);
    if (plt->vid_close)
        NC(gadget, plt->vid_close, (u64)plt->video_handle, 0,0,0,0,0);
    plt_log(plt, "Shutdown\n");
}

static u32 scale_row[DOOM_W * DOOM_SCALE_X] __attribute__((aligned(16)));

void plt_video_flip(struct platform *plt, u32 *doom_fb)
{
    u32 *fb = (u32 *)plt->fbs[plt->active];

    for (int y = 0; y < DOOM_H; y++)
    {
        const u32 *src = doom_fb + y * DOOM_W;
        u32 *out = scale_row;

        for (int x = 0; x < DOOM_W; x++)
        {
            u32 pixel = src[x] | 0xFF000000;
            out[0] = pixel;
            out[1] = pixel;
            out[2] = pixel;
            out[3] = pixel;
            out[4] = pixel;
            out += DOOM_SCALE_X;
        }

        u32 *dst = fb + (DOOM_OFF_Y + y * DOOM_SCALE_Y) * SCR_W + DOOM_OFF_X;
        for (int sy = 0; sy < DOOM_SCALE_Y; sy++)
        {
            const u64 *row_words = (const u64 *)scale_row;
            u64 *dst_words = (u64 *)dst;
            for (int i = 0; i < (DOOM_W * DOOM_SCALE_X) / 2; i++)
                dst_words[i] = row_words[i];
            dst += SCR_W;
        }
    }

    plt_present(plt);
}

void plt_blank(struct platform *plt)
{
    clear_fb((u32 *)plt->fbs[0]);
    clear_fb((u32 *)plt->fbs[1]);
    plt_present(plt);
}

void plt_present(struct platform *plt)
{
    void *gadget = plt->gadget;

    NC(gadget, plt->vid_flip, (u64)plt->video_handle, (u64)plt->active, 1,
       (u64)plt->total_frames, 0, 0);

    plt->flips_pending++;

    u64 wait_start_us = plt->get_proc_time
                        ? NC(gadget, plt->get_proc_time, 0,0,0,0,0,0)
                        : 0;

    if (plt->flips_pending > FLIP_AHEAD)
    {
        if (plt->eq && plt->wait_eq)
        {
            u8 event[64];
            s32 event_count = 0;
            NC(gadget, plt->wait_eq, plt->eq, (u64)event, 1,
               (u64)&event_count, 0, 0);
        }
        else
        {
            if (plt->usleep)
                NC(gadget, plt->usleep, VSYNC_FALLBACK_US, 0, 0, 0, 0, 0);
        }
        plt->flips_pending--;
    }

    if (wait_start_us)
        plt->vsync_block_us += NC(gadget, plt->get_proc_time, 0,0,0,0,0,0)
                               - wait_start_us;

    plt->active ^= 1;
    plt->total_frames++;
}

u32 plt_pad_read(struct platform *plt)
{
    if (plt->pad_handle < 0 || !plt->pad_read) return PLT_PAD_NONE;
    void *gadget = plt->gadget;
    for (int i = 0; i < 128; i++) plt->pad_buf[i] = 0;
    s32 ret = (s32)NC(gadget, plt->pad_read, (u64)plt->pad_handle,
                      (u64)plt->pad_buf, 1, 0, 0, 0);
    if (ret <= 0 || (u32)ret >= 0x80000000) return PLT_PAD_NONE;

    /* ScePadData: button word at 0, leftStick{x,y} at 4, rightStick{x,y} at 6,
       each 0..255 with 128 at rest. Bit 31 of the button word means the system
       UI has taken the pad and the word is not ours; 0x001FFFFF are buttons. */
    plt->stick_lx = (s32)plt->pad_buf[4] - 128;
    plt->stick_ly = (s32)plt->pad_buf[5] - 128;
    plt->stick_rx = (s32)plt->pad_buf[6] - 128;
    plt->stick_ry = (s32)plt->pad_buf[7] - 128;

    u32 raw = *(u32 *)plt->pad_buf;
    if (raw & 0x80000000) return PLT_PAD_NONE;
    return raw & 0x001FFFFF;
}

int plt_thread_start(struct platform *plt, void *(*entry)(void *),
                     void *arg, const char *name)
{
    static u64 tid[MAX_THREADS];
    static int next_slot;

    if (!plt->thread_create || next_slot >= MAX_THREADS) return -1;

    s32 ret = (s32)NC(plt->gadget, plt->thread_create, (u64)&tid[next_slot], 0,
                      (u64)entry, (u64)arg, (u64)name, 0);
    if (ret != 0)
    {
        plt_log(plt, "thread: scePthreadCreate failed\n");
        return -1;
    }
    next_slot++;
    return 0;
}

u32 plt_get_ms(struct platform *plt)
{
    if (plt->get_proc_time)
    {
        u64 now = NC(plt->gadget, plt->get_proc_time, 0,0,0,0,0,0);
        return (u32)((now - plt->start_time_us) / 1000);
    }
    return (plt->total_frames * 1000) / 60;
}

void plt_sleep_ms(struct platform *plt, u32 ms)
{
    if (plt->usleep)
        NC(plt->gadget, plt->usleep, (u64)(ms * 1000), 0,0,0,0,0);
}

#define SD_RO             1
#define SD_RW             2
#define SD_CREATE         4
#define SD_BLOCKS         32768
#define SD_UMOUNT_TRIES   20
#define SD_UMOUNT_POLL_US 250000
#define SD_UMOUNT_WAIT_US 500000

/* Region-dependent, so both are tried and the one holding VMC0.card wins.
   Never with CREATE: on the absent name it builds a second empty container. */
const u8 plt_save_dirs[2][16] = { "SLES-50366", "SLUS-20268" };

s32 plt_savedata_raw_mount(struct platform *plt, void *mount_fn,
                           const u8 *dir, u32 mode, u64 blocks, u8 *result64)
{
    u8 params[128];
    u8 dir_name[32];
    for (int i = 0; i < 128; i++) params[i] = 0;
    for (int i = 0; i < 64; i++) result64[i] = 0;
    for (int i = 0; i < 32; i++) dir_name[i] = 0;
    for (int i = 0; i < 31 && dir[i]; i++) dir_name[i] = dir[i];

    if (!mount_fn) return -1;

    *(u32 *)(params + 0x00) = (u32)plt->user_id;
    *(u64 *)(params + 0x10) = (u64)dir_name;
    *(u64 *)(params + 0x20) = blocks;
    *(u32 *)(params + 0x28) = mode;

    return (s32)NC(plt->gadget, mount_fn, (u64)params, (u64)result64, 0,0,0,0);
}

static void *sd_mount_fn(struct platform *plt)
{
    return plt->save_mount_got ? plt->save_mount_got : plt->save_mount;
}

static s32 sd_mount_mode(struct platform *plt, u32 mode, u8 *result64)
{
    return plt_savedata_raw_mount(plt, sd_mount_fn(plt), plt->save_dir, mode,
                                  SD_BLOCKS, result64);
}

static void sd_keep_dir(struct platform *plt, const u8 *dir)
{
    for (int i = 0; i < 32; i++) plt->save_dir[i] = 0;
    for (int i = 0; i < 15 && dir[i]; i++) plt->save_dir[i] = dir[i];
}

static int sd_mount_any(struct platform *plt, void *mount_fn, u32 mode,
                        u8 *result64)
{
    for (int i = 0; i < 2; i++)
    {
        if (plt_savedata_raw_mount(plt, mount_fn, plt_save_dirs[i], mode,
                                   SD_BLOCKS, result64) == 0)
        {
            sd_keep_dir(plt, plt_save_dirs[i]);
            return 1;
        }
    }
    return 0;
}

static s32 sd_unmount(struct platform *plt)
{
    if (!plt->save_umount) return -1;
    u8 mount_point[32];
    for (int i = 0; i < 32; i++) mount_point[i] = 0;
    const char *path = "/savedata0";
    for (int i = 0; path[i]; i++) mount_point[i] = path[i];

    s32 ret = (s32)NC(plt->gadget, plt->save_umount, (u64)mount_point,
                      0, 0, 0, 0, 0);

    if (plt->kstat)
    {
        u8 stat_buf[256];
        for (int i = 0; i < SD_UMOUNT_TRIES; i++)
        {
            if ((s32)NC(plt->gadget, plt->kstat, (u64)"/savedata0",
                        (u64)stat_buf, 0,0,0,0) != 0)
                break;
            NC(plt->gadget, plt->usleep, SD_UMOUNT_POLL_US, 0, 0, 0, 0, 0);
        }
    }
    else
    {
        NC(plt->gadget, plt->usleep, SD_UMOUNT_WAIT_US, 0, 0, 0, 0, 0);
    }
    return ret;
}

s32 plt_savedata_raw_umount(struct platform *plt) { return sd_unmount(plt); }

static int sd_mounted(struct platform *plt)
{
    u8 stat_buf[256];
    if (!plt->kstat) return -1;
    return (s32)NC(plt->gadget, plt->kstat, (u64)"/savedata0", (u64)stat_buf,
                   0,0,0,0) == 0;
}

int plt_savedata_restore_ro(struct platform *plt)
{
    u8 result[64];
    if (sd_mounted(plt) == 1) return 1;

    void *mount_fns[2];
    int mount_fn_count = 0;
    if (plt->save_mount_got) mount_fns[mount_fn_count++] = plt->save_mount_got;
    if (plt->save_mount)     mount_fns[mount_fn_count++] = plt->save_mount;

    for (int i = 0; i < mount_fn_count; i++)
        if (sd_mount_any(plt, mount_fns[i], SD_RO, result)) return 1;

    plt_log(plt, "savedata: RESTORE FAILED - close and relaunch the title\n");
    return 0;
}

int plt_savedata_check(struct platform *plt, const char *title_id)
{
    (void)title_id;

    if (!plt->save_mount && !plt->save_mount_got)
    {
        plt_log(plt, "savedata: no mount sym\n");
        plt->savedata_ok = 0;
        return -1;
    }

    if (sd_mounted(plt) == 1)
    {
        plt_log(plt, "savedata: /savedata0 present (launcher RO mount)\n");
        plt->savedata_ok = 1;
        return 0;
    }

    plt_log(plt, "savedata: /savedata0 missing at startup\n");
    plt->savedata_ok = plt_savedata_restore_ro(plt);
    return plt->savedata_ok ? 0 : -1;
}

int plt_savedata_begin_write(struct platform *plt)
{
    u8 result[64];
    if (!plt->savedata_ok) return -1;

    /* the unmount is what commits the write, and every RW mount has to be
       paired with one or the console hangs on close */
    sd_unmount(plt);

    if (plt->save_dir[0] && sd_mount_mode(plt, SD_RW, result) == 0) return 0;

    if (sd_mount_any(plt, sd_mount_fn(plt), SD_RW, result)) return 0;

    plt_savedata_restore_ro(plt);
    return -1;
}

int plt_savedata_end_write(struct platform *plt)
{
    s32 ret = sd_unmount(plt);
    plt_savedata_restore_ro(plt);
    return ret;
}
