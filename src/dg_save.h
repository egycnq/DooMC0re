#ifndef DG_SAVE_H
#define DG_SAVE_H

#define DG_SAVE_DIR     "/savedata0/.savegame/"
#define DG_SAVE_SCRATCH "/av_contents/content_tmp/"

void DG_SaveSetDir(const char *dir);

int  DG_SaveBegin(void);
void DG_SaveEnd(void);

void DG_SaveSanitize(void);

#endif
