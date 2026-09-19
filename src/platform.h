#ifndef PLATFORM_H
#define PLATFORM_H

#include "core.h"

struct ext_args
{
    u64 eboot;          /* 0x00, unused: _start receives the real eboot */
    u64 reserved0;      /* 0x08 */
    u32 frame_count;    /* 0x10, written back by C */
    u32 pad0;           /* 0x14 */
    s32 log_fd;         /* 0x18 */
    s32 reserved1;      /* 0x1C */
    u8  log_addr[16];   /* 0x20 */
    u64 handoff[10];    /* 0x30; [3] userId, [4] FTP ctrl fd, [5] FTP data fd,
                           [6] netgame UDP fd, [7] this console's IP,
                           [8] join address from the launcher: ip then port,
                           [9] the netgame port the launcher bound */
};

struct platform
{
    void *gadget, *dlsym_fn;
    u64   eboot_base;

    void *mmap, *munmap, *usleep;
    void *kopen, *kread, *kwrite, *kclose, *klseek;
    void *kunlink, *kmkdir;
    void *load_mod;
    void *getdents;

    void *thread_create;

    void *sendto, *recvfrom;
    void *accept, *poll, *close_fn, *getsockname;
    void *recv_fn, *send_fn;
    s32   ftp_srv_fd, ftp_data_fd, net_fd;
    u32   local_ip;
    u32   join_ip;      /* both 0 unless doom_launcher.py --connect was used */
    u16   join_port;
    u16   net_port;     /* what the launcher bound; 0 before plt_init */

    void *vid_open, *vid_close, *vid_set_buf, *vid_flip;
    void *vid_register_bufs;
    void *alloc_dm, *map_dm, *dm_size;
    void *create_eq, *wait_eq, *delete_eq;
    s32  video_handle;
    u8  *fbs[2];
    int  active;
    u64  eq;
    u32  total_frames;

    void *aud_open, *aud_out;
    s32   audio_handle;
    u32   audio_grain;

    u64   audio_block_us, vsync_block_us;
    int   flips_pending;

    void *pad_init, *pad_open, *pad_read, *pad_close;
    s32   pad_handle;
    u8    pad_buf[128];
    s32   stick_lx, stick_ly, stick_rx, stick_ry;

    void *log_sendto;
    s32   log_fd;
    u8    log_sa[16];

    u64   start_time_us;
    void *get_proc_time;

    void *save_mount;
    void *save_mount_got;
    void *save_umount, *save_init;
    void *kstat;
    s32   save_mod;
    s32   user_id;
    u8    save_dir[32];
    int   savedata_ok;
};

int  plt_init(struct platform *plt, void *eboot, void *dlsym,
              struct ext_args *ext);
void plt_shutdown(struct platform *plt);

void plt_video_flip(struct platform *plt, u32 *doom_fb);
void plt_present(struct platform *plt);

void plt_blank(struct platform *plt);

#define PLT_PAD_NONE 0xFFFFFFFFu
u32  plt_pad_read(struct platform *plt);

int  plt_thread_start(struct platform *plt, void *(*entry)(void *),
                      void *arg, const char *name);

u32  plt_get_ms(struct platform *plt);
void plt_sleep_ms(struct platform *plt, u32 ms);

void plt_log(struct platform *plt, const char *msg);

int  plt_savedata_check(struct platform *plt, const char *title_id);
int  plt_savedata_begin_write(struct platform *plt);
int  plt_savedata_end_write(struct platform *plt);

extern const u8 plt_save_dirs[2][16];

s32  plt_savedata_raw_mount(struct platform *plt, void *mount_fn,
                            const u8 *dir, u32 mode, u64 blocks, u8 *result64);
s32  plt_savedata_raw_umount(struct platform *plt);

int  plt_savedata_restore_ro(struct platform *plt);

#endif
