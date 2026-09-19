#ifndef DG_NETUI_H
#define DG_NETUI_H

/* included by engine sources, so it must not pull in platform.h */

#define DG_NET_WAIT   0
#define DG_NET_CANCEL 1
#define DG_NET_START  2

int  dg_net_wait(const char *title, const char *line1, const char *line2);

void dg_net_wait_reset(void);

int plt_net_local_ip(char *buf, int max_len);
int plt_net_port(void);

#endif
