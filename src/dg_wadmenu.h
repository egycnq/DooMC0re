#ifndef DG_WADMENU_H
#define DG_WADMENU_H

#include "platform.h"

struct dg_pad_state
{
    u32 held;
    int first_read;
};

u32 dg_pad_pressed(struct platform *plt, struct dg_pad_state *pad);

#define WAD_DIR_USB  "/av_contents/content_tmp/"
#define WAD_DIR_SAVE "/savedata0/"

#define WADMENU_MAX      24
#define WADMENU_PATHLEN  96
#define WADMENU_NAMELEN  40
#define WADMENU_DESCLEN  64

#define WAD_SRC_USB   0
#define WAD_SRC_FTP   1
#define WAD_SRC_SAVE  2

struct wad_choice
{
    char path[WADMENU_PATHLEN];
    char filename[WADMENU_NAMELEN];
    char desc[WADMENU_DESCLEN];
    int  source;
    int  usable;
};

void dg_wad_splash(struct platform *plt, const char *msg);
void dg_status_screen(struct platform *plt, const char *line1,
                      const char *line2);
void dg_status_screen_reset(void);

void dg_wad_error(struct platform *plt, const char *msg);

int dg_wad_scan(struct platform *plt, struct wad_choice *out, int max);

void dg_wad_describe(struct platform *plt, struct wad_choice *wad);

const char *dg_wad_menu(struct platform *plt);

#define DG_NETMODE_HOST 0
#define DG_NETMODE_JOIN 1

struct dg_net_choice
{
    int  mode;
    int  players;
    int  game_type;
    int  skill;
    int  octet[4];
    int  digit_cursor;
    int  port;          /* 0 for the default 2342 */
    char addr[26];
};

int dg_net_menu(struct platform *plt, struct dg_net_choice *net);

int dg_net_offer(struct platform *plt);

#endif
