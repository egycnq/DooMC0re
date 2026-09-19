#include "savedata_probe.h"
#include "libc_stubs.h"

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_CREAT     0x0200
#define O_TRUNC     0x0400
#define O_DIRECTORY 0x20000

#define MOUNT_POINT "/savedata0"
#define VMC_PATH    MOUNT_POINT "/VMC0.card"
#define PROBE_DIR   MOUNT_POINT "/.savegame"
#define PROBE_FILE  PROBE_DIR "/probe.bin"

#define SD_RO     1
#define SD_RW     2
#define SD_BLOCKS 32768

#define PROBE_BYTES 0x2c000
#define CHUNK_SIZE  4096

#define DT_DIR            4
#define DIRENT_RECLEN_OFF 4
#define DIRENT_TYPE_OFF   6
#define DIRENT_NAME_OFF   8
#define DIRENT_BUF        1024
#define DIR_LIST_MAX      10

#define ERR_FACILITY_MASK 0xFFFF0000
#define ERR_POSIX         0x80020000
#define ERR_SAVEDATA      0x809F0000
#define ERR_CODE_MASK     0xFFFF

static u8 chunk_buf[CHUNK_SIZE];

static void fill_chunk(u32 offset)
{
    for (int i = 0; i < CHUNK_SIZE; i++)
        chunk_buf[i] = (u8)((offset + i) * 31u + 7u);
}

static int check_chunk(u32 offset)
{
    for (int i = 0; i < CHUNK_SIZE; i++)
        if (chunk_buf[i] != (u8)((offset + i) * 31u + 7u))
            return 0;
    return 1;
}

static void print_err(const char *label, s32 ret)
{
    u32 code = (u32)ret;
    if (code == 0)
        printf("%s ret=0 OK\n", label);
    else if ((code & ERR_FACILITY_MASK) == ERR_POSIX)
        printf("%s ret=0x%x (errno %d)\n", label, code, code & ERR_CODE_MASK);
    else if ((code & ERR_FACILITY_MASK) == ERR_SAVEDATA)
        printf("%s ret=0x%x (savedata err %d)\n",
               label, code, code & ERR_CODE_MASK);
    else
        printf("%s ret=0x%x\n", label, code);
}

static int mount_point_exists(struct platform *plt)
{
    u8 stat_buf[256];
    if (!plt->kstat) return -1;
    return (s32)NC(plt->gadget, plt->kstat, (u64)MOUNT_POINT,
                   (u64)stat_buf, 0, 0, 0, 0) == 0;
}

static int is_real_container(struct platform *plt)
{
    s32 fd = (s32)NC(plt->gadget, plt->kopen, (u64)VMC_PATH, O_RDONLY,
                     0, 0, 0, 0);
    if (fd < 0) return 0;
    NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);
    return 1;
}

static void list_dir(struct platform *plt, const char *path, int max_entries)
{
    s32 dir_fd = (s32)NC(plt->gadget, plt->kopen, (u64)path, O_DIRECTORY,
                         0, 0, 0, 0);
    if (dir_fd < 0)
    {
        printf("[SD]    %s -> no (0x%x)\n", path, (u32)dir_fd);
        return;
    }

    u8 dirents[DIRENT_BUF];
    int shown = 0;
    while (shown < max_entries)
    {
        s32 n = (s32)NC(plt->gadget, plt->getdents, (u64)dir_fd,
                        (u64)dirents, DIRENT_BUF, 0, 0, 0);
        if (n <= 0) break;
        s32 off = 0;
        while (off < n && shown < max_entries)
        {
            u32 reclen = *(u16 *)(dirents + off + DIRENT_RECLEN_OFF);
            if (reclen == 0) break;
            const char *name = (const char *)(dirents + off + DIRENT_NAME_OFF);
            if (!(name[0] == '.' &&
                  (name[1] == 0 || (name[1] == '.' && name[2] == 0))))
            {
                printf("[SD]        %s%s\n", name,
                       dirents[off + DIRENT_TYPE_OFF] == DT_DIR ? "/" : "");
                shown++;
            }
            off += reclen;
        }
    }
    NC(plt->gadget, plt->kclose, (u64)dir_fd, 0, 0, 0, 0, 0);
}

int plt_savedata_probe(struct platform *plt)
{
    u8 mount_result[64];
    void *mount_fns[2];
    const char *mount_fn_names[2];
    int mount_fn_count = 0;
    int real_fn_idx = -1, real_dir_idx = -1;

    printf("[SD] ========= savedata mount matrix (pass 2) =========\n");
    printf("[SD] userId=0x%x eboot=0x%llx\n", (u32)plt->user_id, plt->eboot_base);
    printf("[SD] mount via dlsym    = %p\n", plt->save_mount);
    printf("[SD] mount via eboot GOT= %p  (+0x%x)\n",
           plt->save_mount_got, EBOOT_SAVEDATA_MOUNT_GOT);
    printf("[SD] umount             = %p\n", plt->save_umount);

    if (plt->save_mount_got)
    {
        mount_fns[mount_fn_count] = plt->save_mount_got;
        mount_fn_names[mount_fn_count] = "GOT  ";
        mount_fn_count++;
    }
    if (plt->save_mount)
    {
        mount_fns[mount_fn_count] = plt->save_mount;
        mount_fn_names[mount_fn_count] = "dlsym";
        mount_fn_count++;
    }
    if (!mount_fn_count)
    {
        printf("[SD] VERDICT: no mount entry point at all\n");
        return 0;
    }

    printf("[SD] baseline : /savedata0 present=%d real=%d\n",
           mount_point_exists(plt), is_real_container(plt));
    list_dir(plt, MOUNT_POINT, DIR_LIST_MAX);

    printf("[SD] --- identify (read-only, no CREATE anywhere) ---\n");
    print_err("[SD] umount RO        ", plt_savedata_raw_umount(plt));
    printf("[SD]    present after umount = %d\n", mount_point_exists(plt));

    for (int fn_idx = 0; fn_idx < mount_fn_count; fn_idx++)
    {
        for (int dir_idx = 0; dir_idx < 2; dir_idx++)
        {
            char label[64];
            snprintf(label, sizeof(label), "[SD] %s RO %s",
                     mount_fn_names[fn_idx],
                     (const char *)plt_save_dirs[dir_idx]);
            s32 mount_ret = plt_savedata_raw_mount(plt, mount_fns[fn_idx],
                                                   plt_save_dirs[dir_idx],
                                                   SD_RO, SD_BLOCKS,
                                                   mount_result);
            print_err(label, mount_ret);
            if (mount_ret != 0) continue;

            int real_container = is_real_container(plt);
            mount_result[31] = 0;
            printf("[SD]      -> '%s' container = %s\n",
                   (const char *)mount_result,
                   real_container ? "REAL (VMC0.card)" : "EMPTY/PHANTOM");
            if (real_container && real_fn_idx < 0)
            {
                real_fn_idx = fn_idx;
                real_dir_idx = dir_idx;
            }
            plt_savedata_raw_umount(plt);
        }
    }

    if (real_fn_idx < 0)
    {
        printf("[SD] no pair mounted the real container\n");
        printf("[SD] restoring whatever will mount...\n");
        int restored = plt_savedata_restore_ro(plt);
        printf("[SD] final: present=%d real=%d\n",
               mount_point_exists(plt), is_real_container(plt));
        printf("[SD] VERDICT: NOT MOUNTABLE by this code. %s\n",
               restored ? "Mount restored."
                        : "NOTHING MOUNTED - relaunch the title.");
        return 0;
    }

    printf("[SD] --- writable? using %s + %s ---\n",
           mount_fn_names[real_fn_idx],
           (const char *)plt_save_dirs[real_dir_idx]);

    s32 ret = plt_savedata_raw_mount(plt, mount_fns[real_fn_idx],
                                     plt_save_dirs[real_dir_idx],
                                     SD_RW, SD_BLOCKS, mount_result);
    print_err("[SD] RW mount        ", ret);

    int wrote = 0, verified = 0, real_rw = 0;
    if (ret == 0)
    {
        real_rw = is_real_container(plt);
        printf("[SD]      -> container = %s\n",
               real_rw ? "REAL (VMC0.card)" : "EMPTY/PHANTOM - not writing");

        if (real_rw)
        {
            ret = (s32)NC(plt->gadget, plt->kmkdir, (u64)PROBE_DIR,
                          (u64)0777, 0, 0, 0, 0);
            printf("[SD] mkdir %s ret=0x%x\n", PROBE_DIR, (u32)ret);

            s32 fd = (s32)NC(plt->gadget, plt->kopen, (u64)PROBE_FILE,
                             O_WRONLY | O_CREAT | O_TRUNC, 0666, 0, 0, 0);
            if (fd < 0)
            {
                printf("[SD] open for write : FAILED 0x%x\n", (u32)fd);
            }
            else
            {
                while (wrote < PROBE_BYTES)
                {
                    fill_chunk((u32)wrote);
                    int want = PROBE_BYTES - wrote;
                    if (want > CHUNK_SIZE)
                        want = CHUNK_SIZE;
                    s32 n = (s32)NC(plt->gadget, plt->kwrite, (u64)fd,
                                    (u64)chunk_buf, (u64)want, 0, 0, 0);
                    if (n != want)
                    {
                        if (n > 0)
                            wrote += n;
                        break;
                    }
                    wrote += want;
                }
                NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);
                printf("[SD] big write      : %d / %d bytes\n",
                       wrote, PROBE_BYTES);
            }
        }
    }

    print_err("[SD] umount (commit) ", plt_savedata_raw_umount(plt));
    int restored = plt_savedata_restore_ro(plt);
    printf("[SD] restore RO      : ok=%d present=%d real=%d\n",
           restored, mount_point_exists(plt), is_real_container(plt));

    if (wrote == PROBE_BYTES)
    {
        s32 fd = (s32)NC(plt->gadget, plt->kopen, (u64)PROBE_FILE, O_RDONLY,
                         0, 0, 0, 0);
        if (fd < 0)
        {
            printf("[SD] read back      : open FAILED 0x%x\n", (u32)fd);
        }
        else
        {
            while (verified < PROBE_BYTES)
            {
                int want = PROBE_BYTES - verified;
                if (want > CHUNK_SIZE)
                    want = CHUNK_SIZE;
                s32 n = (s32)NC(plt->gadget, plt->kread, (u64)fd,
                                (u64)chunk_buf, (u64)want, 0, 0, 0);
                if (n != want) break;
                if (!check_chunk((u32)verified)) break;
                verified += want;
            }
            NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);
            printf("[SD] read back      : %d / %d bytes match\n",
                   verified, PROBE_BYTES);
        }
        list_dir(plt, MOUNT_POINT, DIR_LIST_MAX);
    }

    printf("[SD] -------------------------------------------------\n");
    if (verified == PROBE_BYTES && restored)
    {
        printf("[SD] VERDICT: SAVES WORK. %s + %s, mode RW, no CREATE.\n",
               mount_fn_names[real_fn_idx],
               (const char *)plt_save_dirs[real_dir_idx]);
        printf("[SD]   %d KB committed to the real container and read back\n",
               PROBE_BYTES / 1024);
        printf("[SD]   through the restored read-only mount.\n");
    }
    else if (real_rw && !restored)
    {
        printf("[SD] VERDICT: WRITABLE BUT UNSAFE - RO mount did not come back.\n");
        printf("[SD]   Relaunch the title before testing again.\n");
    }
    else if (ret == 0 && !real_rw)
    {
        printf("[SD] VERDICT: RW mount landed on a phantom container. Nothing\n");
        printf("[SD]   was written. The name pairing is still wrong.\n");
    }
    else
    {
        printf("[SD] VERDICT: READ-ONLY. The real container mounts RO but\n");
        printf("[SD]   refuses RW; the emulator holds it for the session.\n");
        printf("[SD]   Saves must live in /av_contents/content_tmp/ instead.\n");
    }
    printf("[SD] =================================================\n");

    return verified == PROBE_BYTES && restored;
}
