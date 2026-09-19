#ifndef DG_FTP_H
#define DG_FTP_H

#include "platform.h"
#include "dg_wadmenu.h"

#define FTP_PORT      1337
#define FTP_DATA_PORT 1338

/* Must stay WAD_DIR_USB: an upload landing anywhere else is invisible to the list. */
#define FTP_DEST      WAD_DIR_USB

int dg_ftp_serve(struct platform *plt);

int dg_ftp_was_uploaded(const char *name);

#endif
