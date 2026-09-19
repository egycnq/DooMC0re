#include "dg_wadmenu.h"
#include "dg_ftp.h"
#include "dg_netui.h"
#include "libc_stubs.h"

extern struct platform g_plt;

#define O_RDONLY    0x0000
#define O_DIRECTORY 0x20000
#define SEEK_SET_   0

#define DT_DIR            4
#define DIRENT_RECLEN_OFF 4
#define DIRENT_TYPE_OFF   6
#define DIRENT_NAME_OFF   8
#define DIRENT_BUF        1024

static const u8 font_data[][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    {0},{0},{0},{0},{0},
    {0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0},
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    {0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x7E,0x00,0x00},
    {0x3C,0x42,0x02,0x3C,0x40,0x7E,0x00,0x00},
    {0x3C,0x42,0x0C,0x02,0x42,0x3C,0x00,0x00},
    {0x08,0x18,0x28,0x48,0x7E,0x08,0x00,0x00},
    {0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00,0x00},
    {0x1C,0x20,0x40,0x7C,0x42,0x3C,0x00,0x00},
    {0x7E,0x02,0x04,0x08,0x10,0x10,0x00,0x00},
    {0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00},
    {0x3C,0x42,0x3E,0x02,0x04,0x38,0x00,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    {0},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
    {0x3C,0x42,0x04,0x08,0x00,0x08,0x00,0x00},
    {0},
    {0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00},
    {0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,0x00},
    {0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00},
    {0x78,0x44,0x42,0x42,0x44,0x78,0x00,0x00},
    {0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00,0x00},
    {0x7E,0x40,0x7C,0x40,0x40,0x40,0x00,0x00},
    {0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00},
    {0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x7E,0x00,0x00},
    {0x1E,0x06,0x06,0x06,0x46,0x3C,0x00,0x00},
    {0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00},
    {0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00},
    {0x42,0x66,0x5A,0x42,0x42,0x42,0x00,0x00},
    {0x42,0x62,0x52,0x4A,0x46,0x42,0x00,0x00},
    {0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00},
    {0x7C,0x42,0x42,0x7C,0x40,0x40,0x00,0x00},
    {0x3C,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00},
    {0x7C,0x42,0x42,0x7C,0x44,0x42,0x00,0x00},
    {0x3C,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    {0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00},
    {0x42,0x42,0x42,0x42,0x24,0x18,0x00,0x00},
    {0x42,0x42,0x42,0x5A,0x66,0x42,0x00,0x00},
    {0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00},
    {0x42,0x42,0x24,0x18,0x18,0x18,0x00,0x00},
    {0x7E,0x04,0x08,0x10,0x20,0x7E,0x00,0x00},
};
#define FONT_FIRST 32
#define FONT_LAST  90

#define DS_CROSS    0x00004000u
#define DS_CIRCLE   0x00002000u
#define DS_TRIANGLE 0x00001000u
#define DS_R1       0x00000800u
#define DS_UP       0x00000010u
#define DS_DOWN     0x00000040u
#define DS_LEFT     0x00000080u
#define DS_RIGHT    0x00000020u

#define STICK_THRESHOLD 48

#define COL_BG     0xFF0A0806u
#define COL_PANEL  0xFF1C1410u
#define COL_BORDER 0xFF6B2B0Fu
#define COL_TITLE  0xFFCF2A0Eu
#define COL_TEXT   0xFFC7A87Bu
#define COL_DIM    0xFF7A6444u
#define COL_BAR    0xFF9E1B0Cu
#define COL_BARTXT 0xFFF0D8A8u
#define COL_ACCENT 0xFFD07B2Cu

#define PAN_X 96
#define PAN_Y 56
#define PAN_W (SCR_W - PAN_X * 2)
#define PAN_H (SCR_H - PAN_Y * 2)

static void fill_rect(u32 *fb, int x, int y, int w, int h, u32 col)
{
    for (int yy = y; yy < y + h; yy++)
    {
        if (yy < 0 || yy >= SCR_H) continue;
        u32 *row = fb + yy * SCR_W;
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < SCR_W) row[xx] = col;
    }
}

static void draw_char(u32 *fb, int x, int y, char ch, int scale, u32 col, int bold)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < FONT_FIRST || ch > FONT_LAST) ch = FONT_FIRST;
    const u8 *glyph = font_data[ch - FONT_FIRST];

    for (int row = 0; row < 8; row++)
    {
        u8 bits = glyph[row];
        if (!bits) continue;
        for (int bit = 0; bit < 8; bit++)
        {
            if (!(bits & (0x80 >> bit))) continue;
            fill_rect(fb, x + bit * scale, y + row * scale,
                      scale + bold, scale, col);
        }
    }
}

#define CELL_W(scale) ((scale) * 6)

static void draw_str(u32 *fb, int x, int y, const char *text, int scale,
                     u32 col)
{
    for (int i = 0; text[i]; i++)
        draw_char(fb, x + i * CELL_W(scale), y, text[i], scale, col, 1);
}

static int str_px(const char *text, int scale)
{
    return (int)strlen(text) * CELL_W(scale);
}

static void draw_centered(u32 *fb, int y, const char *text, int scale, u32 col)
{
    draw_str(fb, (SCR_W - str_px(text, scale)) / 2, y, text, scale, col);
}

static void draw_rule(u32 *fb, int y, int inset)
{
    fill_rect(fb, inset, y, SCR_W - inset * 2, 2, COL_BORDER);
}

#define RULE_WIDE  240
#define RULE_PANEL (PAN_X + 40)

static void draw_frame(u32 *fb, int x, int y, int w, int h, u32 col)
{
    fill_rect(fb, x,          y,          w,  3, col);
    fill_rect(fb, x,          y + 9,      w,  2, col);
    fill_rect(fb, x,          y + h - 3,  w,  3, col);
    fill_rect(fb, x,          y + h - 11, w,  2, col);
    fill_rect(fb, x,          y,          3,  h, col);
    fill_rect(fb, x + 9,      y,          2,  h, col);
    fill_rect(fb, x + w - 3,  y,          3,  h, col);
    fill_rect(fb, x + w - 11, y,          2,  h, col);
}

/* Must be the last thing a screen draws: it halves what is already in the buffer. */
static void apply_scanlines(u32 *fb)
{
    for (int y = 2; y < SCR_H; y += 3)
    {
        u32 *row = fb + y * SCR_W;
        for (int x = 0; x < SCR_W; x++)
        {
            u32 pixel = row[x];
            row[x] = 0xFF000000u | ((pixel & 0x00FEFEFEu) >> 1);
        }
    }
}

static int str_eq_n(const char *a, const char *b, int len)
{
    return strncasecmp(a, b, (size_t)len) == 0;
}

static int name_has(const char *haystack, const char *needle)
{
    int needle_len = (int)strlen(needle);
    for (int i = 0; haystack[i]; i++)
        if (str_eq_n(haystack + i, needle, needle_len)) return 1;
    return 0;
}

#define WAD_HEADER_SIZE   12
#define MAX_LUMPS         40000
#define LUMP_ENTRY_SIZE   16
#define LUMP_NAME_OFFSET  8
#define LUMP_BATCH        64

static void set_desc(struct wad_choice *wad, const char *text)
{
    snprintf(wad->desc, sizeof(wad->desc), "%s", text);
}

void dg_wad_describe(struct platform *plt, struct wad_choice *wad)
{
    u8 hdr[WAD_HEADER_SIZE];
    wad->desc[0] = 0;
    wad->usable = 1;

    s32 fd = (s32)NC(plt->gadget, plt->kopen, (u64)wad->path,
                     O_RDONLY, 0, 0, 0, 0);
    if (fd < 0)
    {
        wad->usable = 0;
        set_desc(wad, "CANNOT OPEN");
        return;
    }

    if ((s32)NC(plt->gadget, plt->kread, (u64)fd, (u64)hdr,
                WAD_HEADER_SIZE, 0, 0, 0) != WAD_HEADER_SIZE)
    {
        NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);
        wad->usable = 0;
        set_desc(wad, "TRUNCATED");
        return;
    }

    int is_iwad = (hdr[0] == 'I' && hdr[1] == 'W' && hdr[2] == 'A' && hdr[3] == 'D');
    int is_pwad = (hdr[0] == 'P' && hdr[1] == 'W' && hdr[2] == 'A' && hdr[3] == 'D');
    if (!is_iwad && !is_pwad)
    {
        NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);
        wad->usable = 0;
        set_desc(wad, "NOT A WAD FILE");
        return;
    }

    u32 numlumps = (u32)hdr[4] | ((u32)hdr[5] << 8) |
                   ((u32)hdr[6] << 16) | ((u32)hdr[7] << 24);
    u32 dirofs   = (u32)hdr[8] | ((u32)hdr[9] << 8) |
                   ((u32)hdr[10] << 16) | ((u32)hdr[11] << 24);
    if (numlumps == 0 || numlumps > MAX_LUMPS)
    {
        NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);
        wad->usable = 0;
        set_desc(wad, "BAD DIRECTORY");
        return;
    }

    NC(plt->gadget, plt->klseek, (u64)fd, (u64)dirofs, SEEK_SET_, 0, 0, 0);

    int has_e1m1 = 0, has_e2m1 = 0, has_e4m1 = 0, has_map01 = 0, has_freedoom = 0;
    int has_playpal = 0, has_texture1 = 0, has_pnames = 0;
    int has_fstart = 0, has_fend = 0, has_sstart = 0, has_send = 0;
    u8 lump_dir[LUMP_BATCH * LUMP_ENTRY_SIZE];
    u32 left = numlumps;

    while (left > 0)
    {
        u32 batch = left > LUMP_BATCH ? LUMP_BATCH : left;
        s32 n = (s32)NC(plt->gadget, plt->kread, (u64)fd, (u64)lump_dir,
                        (u64)(batch * LUMP_ENTRY_SIZE), 0, 0, 0);
        if (n < (s32)(batch * LUMP_ENTRY_SIZE)) break;

        for (u32 i = 0; i < batch; i++)
        {
            const char *name = (const char *)(lump_dir + i * LUMP_ENTRY_SIZE +
                                              LUMP_NAME_OFFSET);
            if (str_eq_n(name, "E1M1", 4))          has_e1m1 = 1;
            else if (str_eq_n(name, "E2M1", 4))     has_e2m1 = 1;
            else if (str_eq_n(name, "E4M1", 4))     has_e4m1 = 1;
            else if (str_eq_n(name, "MAP01", 5))    has_map01 = 1;
            else if (str_eq_n(name, "FREEDOOM", 8)) has_freedoom = 1;
            else if (str_eq_n(name, "PLAYPAL", 7))  has_playpal = 1;
            else if (str_eq_n(name, "TEXTURE1", 8)) has_texture1 = 1;
            else if (str_eq_n(name, "PNAMES", 6))   has_pnames = 1;
            else if (str_eq_n(name, "F_START", 7))  has_fstart = 1;
            else if (str_eq_n(name, "F_END", 5))    has_fend = 1;
            else if (str_eq_n(name, "S_START", 7))  has_sstart = 1;
            else if (str_eq_n(name, "S_END", 5))    has_send = 1;
        }
        left -= batch;
    }
    NC(plt->gadget, plt->kclose, (u64)fd, 0, 0, 0, 0, 0);

    if (is_pwad)
    {
        wad->usable = 0;
        set_desc(wad, has_map01 ? "PWAD (DOOM II MAPS) - NEEDS AN IWAD"
                                : (has_e1m1 ? "PWAD (DOOM MAPS) - NEEDS AN IWAD"
                                            : "PWAD - NEEDS AN IWAD"));
        return;
    }

    if (!has_playpal || !has_texture1 || !has_pnames ||
        !has_fstart || !has_fend || !has_sstart || !has_send)
    {
        wad->usable = 0;
        const char *missing = !has_playpal  ? "PLAYPAL"
                            : !has_texture1 ? "TEXTURE1"
                            : !has_pnames   ? "PNAMES"
                            : !has_fstart   ? "F_START"
                            : !has_fend     ? "F_END"
                            : !has_sstart   ? "S_START" : "S_END";
        snprintf(wad->desc, sizeof(wad->desc), "INCOMPLETE - NO %s", missing);
        return;
    }

    if (has_map01)
    {
        /* Doom II, Plutonia and TNT share MAP01..MAP32;
           only the filename separates them. */
        if (name_has(wad->filename, "PLUTONIA"))
            set_desc(wad, "FINAL DOOM: PLUTONIA");
        else if (name_has(wad->filename, "TNT"))
            set_desc(wad, "FINAL DOOM: TNT EVILUTION");
        else if (name_has(wad->filename, "FREEDM"))
            set_desc(wad, "FREEDM");
        else if (has_freedoom || name_has(wad->filename, "FREEDOOM"))
            set_desc(wad, "FREEDOOM PHASE 2");
        else if (name_has(wad->filename, "HACX"))
            set_desc(wad, "HACX");
        else
            set_desc(wad, "DOOM II: HELL ON EARTH");
        return;
    }

    if (has_e4m1)
        set_desc(wad, "THE ULTIMATE DOOM");
    else if (has_e2m1)
        set_desc(wad, has_freedoom || name_has(wad->filename, "FREEDOOM")
                      ? "FREEDOOM PHASE 1" : "DOOM (REGISTERED)");
    else if (has_e1m1)
        set_desc(wad, name_has(wad->filename, "CHEX") ? "CHEX QUEST"
                                                      : "DOOM SHAREWARE");
}

static int ends_wad(const char *name)
{
    int len = (int)strlen(name);
    return len > 4 && str_eq_n(name + len - 4, ".WAD", 4);
}

static int scan_dir(struct platform *plt, const char *dir, int source,
                    struct wad_choice *out, int count, int max)
{
    s32 dir_fd = (s32)NC(plt->gadget, plt->kopen, (u64)dir,
                         O_DIRECTORY, 0, 0, 0, 0);
    if (dir_fd < 0) return count;

    u8 dirents[DIRENT_BUF];
    for (;;)
    {
        s32 n = (s32)NC(plt->gadget, plt->getdents, (u64)dir_fd,
                        (u64)dirents, DIRENT_BUF, 0, 0, 0);
        if (n <= 0) break;
        s32 off = 0;
        while (off < n && count < max)
        {
            u32 reclen = *(u16 *)(dirents + off + DIRENT_RECLEN_OFF);
            if (reclen == 0) break;
            u8 d_type = dirents[off + DIRENT_TYPE_OFF];
            const char *name = (const char *)(dirents + off + DIRENT_NAME_OFF);
            off += reclen;

            if (d_type == DT_DIR) continue;
            if (!ends_wad(name)) continue;

            int dup = 0;
            for (int i = 0; i < count; i++)
                if (!strcasecmp(out[i].filename, name))
            {
                    dup = 1;
                    break;
                }
            if (dup) continue;

            struct wad_choice *wad = &out[count];
            snprintf(wad->filename, sizeof(wad->filename), "%s", name);
            snprintf(wad->path, sizeof(wad->path), "%s%s", dir, name);
            wad->source = (source == WAD_SRC_USB &&
                           dg_ftp_was_uploaded(wad->filename))
                          ? WAD_SRC_FTP : source;
            wad->desc[0] = 0;
            count++;
        }
        if (count >= max) break;
    }
    NC(plt->gadget, plt->kclose, (u64)dir_fd, 0, 0, 0, 0, 0);
    return count;
}

static void draw_shell(u32 *fb)
{
    const char *caption = " DOOM WAD LOADER ";
    int caption_w;

    fill_rect(fb, 0, 0, SCR_W, SCR_H, COL_BG);
    fill_rect(fb, PAN_X, PAN_Y, PAN_W, PAN_H, COL_PANEL);
    draw_frame(fb, PAN_X, PAN_Y, PAN_W, PAN_H, COL_BORDER);

    caption_w = str_px(caption, 4);
    fill_rect(fb, (SCR_W - caption_w) / 2 - 16, PAN_Y - 22,
              caption_w + 32, 62, COL_PANEL);
    draw_str(fb, (SCR_W - caption_w) / 2, PAN_Y - 14, caption, 4, COL_TITLE);

    draw_centered(fb, PAN_Y + 84, "EGYDEVTEAM", 2, COL_ACCENT);
}

static void draw_heading(u32 *fb, const char *text)
{
    draw_rule(fb, PAN_Y + 132, RULE_PANEL);
    draw_str(fb, PAN_X + 56, PAN_Y + 152, text, 3, COL_TEXT);
    draw_rule(fb, PAN_Y + 200, RULE_PANEL);
}

static void draw_help(u32 *fb, const char *text)
{
    fill_rect(fb, PAN_X + 40, SCR_H - PAN_Y - 78, PAN_W - 80, 48, COL_BAR);
    draw_str(fb, PAN_X + 60, SCR_H - PAN_Y - 68, text, 2, COL_BARTXT);
}

void dg_wad_splash(struct platform *plt, const char *msg)
{
    u32 *fb = (u32 *)plt->fbs[plt->active];

    draw_shell(fb);
    draw_rule(fb, SCR_H / 2 - 50, RULE_WIDE);
    draw_centered(fb, SCR_H / 2 - 12, msg, 3, COL_TEXT);
    draw_rule(fb, SCR_H / 2 + 56, RULE_WIDE);
    apply_scanlines(fb);

    plt_present(plt);
}

static void draw_error(u32 *fb, const char *msg)
{
    int y = SCR_H / 2 - 60;
    int i = 0;

    draw_shell(fb);
    draw_centered(fb, SCR_H / 2 - 150, "THE ENGINE STOPPED", 3, COL_TITLE);
    draw_rule(fb, SCR_H / 2 - 100, RULE_WIDE);

    while (msg[i] && y < SCR_H - 220)
    {
        char line[52];
        int n = 0;
        while (msg[i] && n < (int)sizeof(line) - 1)
            line[n++] = msg[i++];
        line[n] = 0;
        draw_str(fb, PAN_X + 60, y, line, 2, COL_TEXT);
        y += 34;
    }

    draw_rule(fb, SCR_H - 200, RULE_WIDE);
    draw_centered(fb, SCR_H - 170, "THIS WAD CANNOT BE LOADED", 2, COL_DIM);
    draw_centered(fb, SCR_H - 130, "CLOSE THE GAME FROM THE PS MENU", 2, COL_DIM);
    apply_scanlines(fb);
}

#define ERROR_HOLD_FRAMES (60 * 30)

void dg_wad_error(struct platform *plt, const char *msg)
{
    for (int frame = 0; frame < ERROR_HOLD_FRAMES; frame++)
    {
        if (frame < 2) draw_error((u32 *)plt->fbs[plt->active], msg);
        plt_present(plt);
    }
}

static int cache_str(char *dst, int max, const char *src)
{
    int changed = 0, i;

    for (i = 0; i < max - 1 && src[i]; i++)
    {
        if (dst[i] != src[i]) changed = 1;
        dst[i] = src[i];
    }
    if (dst[i]) changed = 1;
    dst[i] = 0;
    return changed;
}

static char status_line1[64], status_line2[64];
static int  status_dirty = 2;

void dg_status_screen_reset(void)
{
    status_line1[0] = 0;
    status_line2[0] = 0;
    status_dirty = 2;
}

void dg_status_screen(struct platform *plt, const char *line1,
                      const char *line2)
{
    int moved = cache_str(status_line1, sizeof(status_line1), line1);

    moved |= cache_str(status_line2, sizeof(status_line2), line2);
    /* two framebuffers, so paint every change into both */
    if (moved) status_dirty = 2;

    if (status_dirty > 0)
    {
        u32 *fb = (u32 *)plt->fbs[plt->active];
        status_dirty--;

        draw_shell(fb);
        draw_rule(fb, SCR_H / 2 - 110, RULE_WIDE);
        draw_centered(fb, SCR_H / 2 - 70, status_line1, 3, COL_TITLE);
        draw_centered(fb, SCR_H / 2 + 10, status_line2, 2, COL_TEXT);
        draw_rule(fb, SCR_H / 2 + 80, RULE_WIDE);
        draw_centered(fb, SCR_H - 100, "O  BACK", 2, COL_DIM);
        apply_scanlines(fb);
    }

    plt_present(plt);
}

int dg_wad_scan(struct platform *plt, struct wad_choice *out, int max)
{
    int count = 0;

    dg_wad_splash(plt, "SCANNING FOR WAD FILES");
    count = scan_dir(plt, WAD_DIR_USB,  WAD_SRC_USB,  out, count, max);
    count = scan_dir(plt, WAD_DIR_SAVE, WAD_SRC_SAVE, out, count, max);

    for (int i = 0; i < count; i++)
    {
        char line[80];
        snprintf(line, sizeof(line), "READING %s  (%d/%d)",
                 out[i].filename, i + 1, count);
        dg_wad_splash(plt, line);
        dg_wad_describe(plt, &out[i]);
    }
    return count;
}

#define REPEAT_DELAY  24
#define REPEAT_PERIOD 6

struct dir_state
{
    int last;
    int hold;
};

static int dir_step(struct dir_state *state, int dir)
{
    int step = 0;

    if (dir != state->last)
    {
        step = dir;
        state->hold = 0;
    }
    else if (dir && ++state->hold > REPEAT_DELAY &&
               (state->hold % REPEAT_PERIOD) == 0)
    {
        step = dir;
    }
    state->last = dir;
    return step;
}

u32 dg_pad_pressed(struct platform *plt, struct dg_pad_state *pad)
{
    u32 buttons = plt_pad_read(plt);
    u32 pressed;

    if (buttons == PLT_PAD_NONE) buttons = pad->held;
    if (pad->first_read)
    {
        pad->held = buttons;
        pad->first_read = 0;
    }

    pressed = buttons & ~pad->held;
    pad->held = buttons;
    return pressed;
}

#if MULTIPLAYER

static char net_title[48], net_line1[48], net_line2[48];
static int  net_dirty = 2;
static struct dg_pad_state net_pad;

void dg_net_wait_reset(void)
{
    net_title[0] = net_line1[0] = net_line2[0] = 0;
    net_dirty = 2;
    net_pad.held = 0;
    net_pad.first_read = 1;
}

static void draw_net_wait(u32 *fb)
{
    char ip[24];

    draw_shell(fb);
    draw_rule(fb, SCR_H / 2 - 130, RULE_WIDE);
    draw_centered(fb, SCR_H / 2 - 90, net_title, 3, COL_TITLE);
    draw_centered(fb, SCR_H / 2 - 10, net_line1, 3, COL_TEXT);
    draw_rule(fb, SCR_H / 2 + 60, RULE_WIDE);

    plt_net_local_ip(ip, sizeof(ip));
    draw_centered(fb, SCR_H / 2 + 100, "THIS CONSOLE", 2, COL_DIM);
    draw_centered(fb, SCR_H / 2 + 140, ip, 3, COL_ACCENT);

    draw_centered(fb, SCR_H - 140, net_line2, 2, COL_DIM);
    apply_scanlines(fb);
}

int dg_net_wait(const char *title, const char *line1, const char *line2)
{
    struct platform *plt = &g_plt;
    int ret = DG_NET_WAIT;
    int moved = cache_str(net_title, sizeof(net_title), title);
    u32 pressed;

    moved |= cache_str(net_line1, sizeof(net_line1), line1);
    moved |= cache_str(net_line2, sizeof(net_line2), line2);
    if (moved) net_dirty = 2;

    if (net_dirty > 0)
    {
        net_dirty--;
        draw_net_wait((u32 *)plt->fbs[plt->active]);
    }

    pressed = dg_pad_pressed(plt, &net_pad);
    if (pressed & DS_CIRCLE)     ret = DG_NET_CANCEL;
    else if (pressed & DS_CROSS) ret = DG_NET_START;

    plt_present(plt);
    return ret;
}

static const char *game_name[3] = { "CO-OP", "DEATHMATCH", "DEATHMATCH 2.0" };
static const char *skill_name[5] = {
    "I'M TOO YOUNG TO DIE", "HEY, NOT TOO ROUGH", "HURT ME PLENTY",
    "ULTRA-VIOLENCE", "NIGHTMARE"
};

enum { ROW_MODE, ROW_ADDR, ROW_PLAYERS, ROW_GAME, ROW_SKILL, ROW_START,
       ROW_COUNT };

#define ADDR_OCTET_DIGITS 3
#define ADDR_DIGITS       (4 * ADDR_OCTET_DIGITS)
#define NET_VALUE_X       (PAN_X + 560)

static int build_rows(const struct dg_net_choice *net, int *rows)
{
    int count = 0;

    rows[count++] = ROW_MODE;
    if (net->mode == DG_NETMODE_JOIN) rows[count++] = ROW_ADDR;
    rows[count++] = ROW_PLAYERS;
    rows[count++] = ROW_GAME;
    rows[count++] = ROW_SKILL;
    rows[count++] = ROW_START;
    return count;
}

static void addr_text(const struct dg_net_choice *net, char *out, int max)
{
    snprintf(out, max, "%03d.%03d.%03d.%03d",
             net->octet[0], net->octet[1], net->octet[2], net->octet[3]);
}

static void addr_bump(struct dg_net_choice *net, int digit_index, int step)
{
    static const int place[3] = { 100, 10, 1 };
    int octet_index = digit_index / ADDR_OCTET_DIGITS;
    int place_index = digit_index % ADDR_OCTET_DIGITS;
    int digit = (net->octet[octet_index] / place[place_index]) % 10;

    net->octet[octet_index] +=
        ((digit + 10 + step) % 10 - digit) * place[place_index];
}

static void addr_clamp(struct dg_net_choice *net)
{
    for (int i = 0; i < 4; i++)
        if (net->octet[i] > 255) net->octet[i] = 255;
}

static void row_value(const struct dg_net_choice *net, int row,
                      char *out, int max)
{
    switch (row)
    {
    case ROW_MODE:
        snprintf(out, max, "%s",
                 net->mode == DG_NETMODE_HOST ? "HOST A GAME" : "JOIN A GAME");
        break;
    case ROW_ADDR:
        addr_text(net, out, max);
        break;
    case ROW_PLAYERS:
        snprintf(out, max, "%d", net->players);
        break;
    case ROW_GAME:
        snprintf(out, max, "%s", game_name[net->game_type]);
        break;
    case ROW_SKILL:
        snprintf(out, max, "%s", skill_name[net->skill - 1]);
        break;
    default:
        out[0] = 0;
        break;
    }
}

static void draw_net_menu(u32 *fb, const struct dg_net_choice *net,
                          const int *rows, int nrows, int sel, int editing)
{
    const int list_y = 380, line_h = 78;
    static const char *label[ROW_COUNT] = {
        "MODE", "ADDRESS", "PLAYERS", "GAME", "SKILL", "START GAME"
    };
    char ip[24], line[64];
    int i;

    draw_shell(fb);
    draw_heading(fb, "NETWORK GAME");

    for (i = 0; i < nrows; i++)
    {
        int y = list_y + i * line_h;
        int selected = (i == sel);
        char value[32];

        if (selected)
            fill_rect(fb, PAN_X + 40, y - 12, PAN_W - 80, line_h - 14, COL_BAR);

        draw_str(fb, PAN_X + 70, y, label[rows[i]], 3,
                 selected ? COL_BARTXT : COL_DIM);
        row_value(net, rows[i], value, sizeof(value));

        if (rows[i] == ROW_ADDR && editing)
        {
            int x = NET_VALUE_X;
            int digit = 0;
            for (int k = 0; value[k]; k++)
            {
                if (value[k] != '.')
                {
                    if (digit == net->digit_cursor)
                        fill_rect(fb, x - 2, y - 6, CELL_W(3) + 2, 30,
                                  COL_ACCENT);
                    digit++;
                }
                draw_char(fb, x, y, value[k], 3, COL_BARTXT, 1);
                x += CELL_W(3);
            }
        }
        else
        {
            draw_str(fb, NET_VALUE_X, y, value, 3,
                     selected ? COL_BARTXT : COL_TEXT);
        }
    }

    plt_net_local_ip(ip, sizeof(ip));
    snprintf(line, sizeof(line), "THIS CONSOLE IS %s", ip);
    draw_rule(fb, SCR_H - PAN_Y - 150, RULE_PANEL);
    draw_str(fb, PAN_X + 70, SCR_H - PAN_Y - 128, line, 2, COL_DIM);

    draw_help(fb, editing
        ? " LEFT/RIGHT DIGIT    UP/DOWN VALUE    X DONE    O CANCEL "
        : rows[sel] == ROW_ADDR
        ? " UP/DOWN ROW    X EDIT ADDRESS    O BACK "
        : " UP/DOWN ROW    LEFT/RIGHT CHANGE    X SELECT    O BACK ");

    apply_scanlines(fb);
}

static void row_change(struct dg_net_choice *net, int row, int step)
{
    switch (row)
    {
    case ROW_MODE:
        net->mode = (net->mode == DG_NETMODE_HOST) ? DG_NETMODE_JOIN
                                                   : DG_NETMODE_HOST;
        break;
    case ROW_PLAYERS:
        net->players = 2 + (net->players - 2 + 3 + step) % 3;
        break;
    case ROW_GAME:
        net->game_type = (net->game_type + 3 + step) % 3;
        break;
    case ROW_SKILL:
        net->skill = 1 + (net->skill - 1 + 5 + step) % 5;
        break;
    default:
        break;
    }
}

int dg_net_menu(struct platform *plt, struct dg_net_choice *net)
{
    struct dg_pad_state pad = { 0, 1 };
    struct dir_state vert = { 0, 0 };
    struct dir_state horiz = { 0, 0 };
    int rows[ROW_COUNT], nrows;
    int sel = 0, editing = 0, dirty = 2;
    int saved_octet[4];

    for (;;)
    {
        u32 pressed = dg_pad_pressed(plt, &pad);
        int vert_dir = 0, horiz_dir = 0, vert_step, horiz_step;

        nrows = build_rows(net, rows);
        if (sel >= nrows) sel = nrows - 1;

        if ((pad.held & DS_DOWN) || plt->stick_ly > STICK_THRESHOLD)
            vert_dir = 1;
        else if ((pad.held & DS_UP) || plt->stick_ly < -STICK_THRESHOLD)
            vert_dir = -1;
        if ((pad.held & DS_RIGHT) || plt->stick_lx > STICK_THRESHOLD)
            horiz_dir = 1;
        else if ((pad.held & DS_LEFT) || plt->stick_lx < -STICK_THRESHOLD)
            horiz_dir = -1;

        vert_step = dir_step(&vert, vert_dir);
        horiz_step = dir_step(&horiz, horiz_dir);

        if (editing)
        {
            if (horiz_step)
                net->digit_cursor = (net->digit_cursor + ADDR_DIGITS +
                                     horiz_step) % ADDR_DIGITS;
            if (vert_step) addr_bump(net, net->digit_cursor, -vert_step);
            if (pressed & (DS_CROSS | DS_CIRCLE))
            {
                if (pressed & DS_CROSS) addr_clamp(net);
                else memcpy(net->octet, saved_octet, sizeof(saved_octet));
                editing = 0;
                dirty = 2;
            }
            if (vert_step || horiz_step) dirty = 2;
        }
        else
        {
            if (vert_step)
            {
                sel = (sel + nrows + vert_step) % nrows;
                dirty = 2;
            }
            if (horiz_step)
            {
                row_change(net, rows[sel], horiz_step);
                nrows = build_rows(net, rows);
                if (sel >= nrows) sel = nrows - 1;
                dirty = 2;
            }

            if (pressed & DS_CROSS)
            {
                if (rows[sel] == ROW_ADDR)
                {
                    memcpy(saved_octet, net->octet, sizeof(saved_octet));
                    editing = 1;
                    net->digit_cursor = 0;
                    dirty = 2;
                }
                else
                {
                    /* the port is not shown on the row: the digit cursor
                       counts characters and a colon would throw it off */
                    if (net->port && net->port != plt_net_port())
                        snprintf(net->addr, sizeof(net->addr),
                                 "%03d.%03d.%03d.%03d:%d",
                                 net->octet[0], net->octet[1], net->octet[2],
                                 net->octet[3], net->port);
                    else
                        addr_text(net, net->addr, sizeof(net->addr));
                    return 1;
                }
            }
            if (pressed & DS_CIRCLE) return 0;
        }

        if (dirty > 0)
        {
            dirty--;
            draw_net_menu((u32 *)plt->fbs[plt->active], net, rows, nrows,
                          sel, editing);
        }

        plt_present(plt);
    }
}

#define NETGAME_OFFER_SECONDS 5

int dg_net_offer(struct platform *plt)
{
    struct dg_pad_state pad = { 0, 1 };
    unsigned start_ms = plt_get_ms(plt);
    int last_seconds_left = -1, dirty = 0;

    for (;;)
    {
        dg_pad_pressed(plt, &pad);
        int seconds_left = NETGAME_OFFER_SECONDS -
                           (int)((plt_get_ms(plt) - start_ms) / 1000);

        /* held, not pressed: dg_pad_pressed adopts whatever is down on its
           first read, so a triangle held through the WAD load would never
           register as an edge */
        if (pad.held & DS_TRIANGLE)
        {
            plt_log(plt, "netgame: triangle\n");
            return 1;
        }
        if (seconds_left <= 0)
        {
            plt_log(plt, "netgame: not requested, booting single player\n");
            return 0;
        }

        if (seconds_left != last_seconds_left)
        {
            last_seconds_left = seconds_left;
            dirty = 2;
        }

        if (dirty > 0)
        {
            u32 *fb = (u32 *)plt->fbs[plt->active];
            char line[48];

            dirty--;
            draw_shell(fb);
            draw_rule(fb, SCR_H / 2 - 50, RULE_WIDE);
            draw_centered(fb, SCR_H / 2 - 12, "LOADING DOOM", 3, COL_TEXT);
            draw_rule(fb, SCR_H / 2 + 56, RULE_WIDE);
            snprintf(line, sizeof(line),
                     "HOLD TRIANGLE FOR NETWORK GAME    (%d)", seconds_left);
            draw_centered(fb, SCR_H / 2 + 110, line, 2, COL_DIM);
            apply_scanlines(fb);
        }

        plt_present(plt);
    }
}

#endif

static struct wad_choice wads[WADMENU_MAX];
static char chosen[WADMENU_PATHLEN];

static void build_label(const struct wad_choice *wad, char *out, int max)
{
    if (wad->desc[0]) snprintf(out, max, "%s - %s", wad->desc, wad->filename);
    else              snprintf(out, max, "%s", wad->filename);
}

static void draw_menu(u32 *fb, int count, int sel, int scroll,
                      int visible, int list_y, int line_h)
{
    static const char *src_name[3] = { "USB", "FTP", "SAVE" };

    draw_shell(fb);
    draw_heading(fb, "SELECT A WAD TO LOAD");

    if (scroll > 0)
        draw_str(fb, SCR_W - PAN_X - 90, list_y - 44, "-", 3, COL_DIM);

    for (int i = 0; i < visible && scroll + i < count; i++)
    {
        int idx = scroll + i;
        int y = list_y + i * line_h;
        int selected = (idx == sel);
        int usable = wads[idx].usable;
        char label[74];
        char num[8];
        u32 col;

        build_label(&wads[idx], label, sizeof(label));
        snprintf(num, sizeof(num), "%d.", idx + 1);

        if (selected && usable)
            fill_rect(fb, PAN_X + 40, y - 12, PAN_W - 80, line_h - 8, COL_BAR);
        if (selected && !usable)
            draw_str(fb, PAN_X + 20, y, ">", 3, COL_BORDER);

        col = !usable ? COL_DIM : selected ? COL_BARTXT : COL_TEXT;

        draw_str(fb, PAN_X + 60,  y, num,   3,
                 (selected && usable) ? COL_BARTXT : COL_DIM);
        draw_str(fb, PAN_X + 130, y, label, 3, col);
        draw_str(fb, SCR_W - PAN_X - 150, y, src_name[wads[idx].source],
                 2, (selected && usable) ? COL_BARTXT : COL_DIM);
    }

    if (scroll + visible < count)
        draw_str(fb, SCR_W - PAN_X - 90, list_y + visible * line_h,
                 "-", 3, COL_DIM);

    draw_help(fb, " UP/DOWN SELECT    X LAUNCH    R1 FTP UPLOAD ");
    apply_scanlines(fb);
}

const char *dg_wad_menu(struct platform *plt)
{
    int count = dg_wad_scan(plt, wads, WADMENU_MAX);

    printf("[WAD] found %d\n", count);
    {
        static const char *tag[3] = { "[USB]", "[FTP]", "[SAVEDATA]" };
        for (int i = 0; i < count; i++)
            printf("[WAD]   %s  %s  (%s)%s\n", wads[i].path,
                   tag[wads[i].source],
                   wads[i].desc[0] ? wads[i].desc : "unidentified",
                   wads[i].usable ? "" : "  UNUSABLE");
    }

    if (count == 0)
    {
        printf("[WAD] none found, starting FTP\n");
        dg_ftp_serve(plt);
        count = dg_wad_scan(plt, wads, WADMENU_MAX);
        printf("[WAD] after FTP: %d\n", count);
    }

    if (count == 0) return 0;
    if (count == 1 && wads[0].usable)
    {
        snprintf(chosen, sizeof(chosen), "%s", wads[0].path);
        return chosen;
    }

    int sel = 0, scroll = 0, dirty = 2;
    struct dg_pad_state pad = { 0, 1 };
    struct dir_state vert = { 0, 0 };
    const int visible = 8;
    const int line_h = 68;
    const int list_y = 344;

    for (;;)
    {
        u32 pressed = dg_pad_pressed(plt, &pad);

        int dir = 0;
        if ((pad.held & DS_DOWN) || plt->stick_ly > STICK_THRESHOLD)
            dir = 1;
        else if ((pad.held & DS_UP) || plt->stick_ly < -STICK_THRESHOLD)
            dir = -1;

        int step = dir_step(&vert, dir);

        if (step) sel = (sel + count + step) % count;

        if ((pressed & DS_CROSS) && wads[sel].usable)
        {
            snprintf(chosen, sizeof(chosen), "%s", wads[sel].path);
            printf("[WAD] selected %s\n", chosen);
            return chosen;
        }

        if (pressed & DS_R1)
        {
            dg_ftp_serve(plt);
            count = dg_wad_scan(plt, wads, WADMENU_MAX);
            printf("[WAD] after FTP: %d\n", count);
            if (count == 0) return 0;
            if (sel >= count) sel = count - 1;
            scroll = 0;
            dirty = 2;
            pad.held = 0;
            pad.first_read = 1;
            continue;
        }

        int old_scroll = scroll;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;

        if (step || scroll != old_scroll) dirty = 2;

        if (dirty > 0)
        {
            dirty--;
            draw_menu((u32 *)plt->fbs[plt->active], count, sel, scroll,
                      visible, list_y, line_h);
        }

        plt_present(plt);
    }
}
