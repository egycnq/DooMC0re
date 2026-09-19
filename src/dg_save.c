#include "dg_save.h"
#include "platform.h"
#include "libc_stubs.h"

#define SAVE_SLOT_COUNT   8
#define SAVE_HEADER_BYTES 4
#define PRINTABLE_MIN     0x20
#define PRINTABLE_MAX     0x7E

extern struct platform g_plt;

static int save_depth;
static char save_dir[96] = DG_SAVE_DIR;

static void mkdir_chain(const char *dir)
{
    char path[sizeof(save_dir)];
    int len = 0;

    while (dir[len] && len < (int)sizeof(path) - 1)
    {
        path[len] = dir[len];
        len++;
    }
    path[len] = 0;
    while (len > 1 && path[len - 1] == '/')
        path[--len] = 0;

    for (int i = 1; i < len; i++)
    {
        if (path[i] != '/') continue;
        path[i] = 0;
        NC(g_plt.gadget, g_plt.kmkdir, (u64)path, (u64)0777, 0, 0, 0, 0);
        path[i] = '/';
    }
    NC(g_plt.gadget, g_plt.kmkdir, (u64)path, (u64)0777, 0, 0, 0, 0);
}

void DG_SaveSetDir(const char *dir)
{
    int i = 0;

    for (; dir[i] && i < (int)sizeof(save_dir) - 1; i++)
        save_dir[i] = dir[i];
    save_dir[i] = 0;
    printf("[SAVE] slots live in %s\n", save_dir);

    DG_SaveSanitize();
}

int DG_SaveBegin(void)
{
    if (save_depth > 0)
    {
        save_depth++;
        return 0;
    }

    if (plt_savedata_begin_write(&g_plt) != 0)
    {
        printf("[SAVE] could not mount savedata read-write\n");
        return -1;
    }

    mkdir_chain(save_dir);

    save_depth = 1;
    return 0;
}

void DG_SaveEnd(void)
{
    if (save_depth <= 0) return;
    if (--save_depth > 0) return;

    plt_savedata_end_write(&g_plt);
}

void DG_SaveSanitize(void)
{
    char path[128];
    u8   head[8];

    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++)
    {
        snprintf(path, sizeof(path), "%sdoomsav%d.dsg", save_dir, slot);

        s32 fd = (s32)NC(g_plt.gadget, g_plt.kopen, (u64)path, 0, 0, 0, 0, 0);
        if (fd < 0) continue;
        s32 head_len = (s32)NC(g_plt.gadget, g_plt.kread, (u64)fd, (u64)head,
                               SAVE_HEADER_BYTES, 0, 0, 0);
        NC(g_plt.gadget, g_plt.kclose, (u64)fd, 0, 0, 0, 0, 0);
        if (head_len < SAVE_HEADER_BYTES) continue;

        /* a savegame starts with its description text */
        int printable = 1;
        for (int i = 0; i < SAVE_HEADER_BYTES; i++)
            if (head[i] < PRINTABLE_MIN || head[i] > PRINTABLE_MAX)
        {
                printable = 0;
                break;
            }
        if (printable) continue;

        printf("[SAVE] slot %d is not a savegame (%02x %02x %02x %02x) - removing\n",
               slot, head[0], head[1], head[2], head[3]);
        if (DG_SaveBegin() == 0)
        {
            NC(g_plt.gadget, g_plt.kunlink, (u64)path, 0, 0, 0, 0, 0);
            DG_SaveEnd();
        }
    }
}
