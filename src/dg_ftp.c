#include "dg_ftp.h"
#include "dg_wadmenu.h"
#include "libc_stubs.h"

#define POLLIN_        0x0001
#define DS_CIRCLE      0x00002000u

#define O_WRONLY_      0x0001
#define O_CREAT_       0x0200
#define O_TRUNC_       0x0400

#define SOCKADDR_LEN    16
#define SOCKADDR_IP_OFF 4

#define FTP_BUF_SIZE       16384
#define FTP_UNAVAIL_FRAMES 240

static u8 transfer_buf[FTP_BUF_SIZE];

static int starts_with(const char *text, const char *prefix)
{
    while (*prefix)
    {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static s32 sock_close(struct platform *plt, s32 fd)
{
    if (fd < 0 || !plt->close_fn) return -1;
    return (s32)NC(plt->gadget, plt->close_fn, (u64)fd, 0, 0, 0, 0, 0);
}

static int sock_readable(struct platform *plt, s32 fd)
{
    u8 pollfd[8];

    if (!plt->poll) return 0;
    *(s32 *)(pollfd + 0) = fd;
    *(u16 *)(pollfd + 4) = POLLIN_;
    *(u16 *)(pollfd + 6) = 0;
    return (s32)NC(plt->gadget, plt->poll, (u64)pollfd, 1, 0, 0, 0, 0) > 0;
}

static s32 sock_accept(struct platform *plt, s32 fd)
{
    u8  sockaddr[SOCKADDR_LEN];
    u32 sockaddr_len = SOCKADDR_LEN;

    if (!plt->accept) return -1;
    return (s32)NC(plt->gadget, plt->accept, (u64)fd, (u64)sockaddr,
                   (u64)&sockaddr_len, 0, 0, 0);
}

static s32 sock_send(struct platform *plt, s32 fd, const void *buf, int n)
{
    if (plt->send_fn)
        return (s32)NC(plt->gadget, plt->send_fn, (u64)fd, (u64)buf, (u64)n,
                       0, 0, 0);
    return (s32)NC(plt->gadget, plt->sendto, (u64)fd, (u64)buf, (u64)n,
                   0, 0, 0);
}

static s32 sock_recv(struct platform *plt, s32 fd, void *buf, int n)
{
    if (plt->recv_fn)
        return (s32)NC(plt->gadget, plt->recv_fn, (u64)fd, (u64)buf, (u64)n,
                       0, 0, 0);
    return (s32)NC(plt->gadget, plt->recvfrom, (u64)fd, (u64)buf, (u64)n,
                   0, 0, 0);
}

static void reply(struct platform *plt, s32 fd, const char *text)
{
    sock_send(plt, fd, text, (int)strlen(text));
}

static u32 local_ip(struct platform *plt, s32 fd)
{
    u8  sockaddr[SOCKADDR_LEN];
    u32 sockaddr_len = SOCKADDR_LEN;

    for (int i = 0; i < SOCKADDR_LEN; i++) sockaddr[i] = 0;
    if (plt->getsockname)
        NC(plt->gadget, plt->getsockname, (u64)fd, (u64)sockaddr,
           (u64)&sockaddr_len, 0, 0, 0);
    return *(u32 *)(sockaddr + SOCKADDR_IP_OFF);
}

static int   files_received;
static u64   bytes_received;
static char  last_file[48];
static int   should_exit;

#define FTP_MAX_UPLOADS 24
static char upload_names[FTP_MAX_UPLOADS][40];
static int  upload_count;

int dg_ftp_was_uploaded(const char *name)
{
    for (int i = 0; i < upload_count; i++)
        if (!strcasecmp(upload_names[i], name)) return 1;
    return 0;
}

static void remember_upload(const char *name)
{
    if (dg_ftp_was_uploaded(name)) return;
    if (upload_count >= FTP_MAX_UPLOADS) return;
    snprintf(upload_names[upload_count], sizeof(upload_names[0]), "%s", name);
    upload_count++;
}

static const char *basename_of(const char *path)
{
    const char *base = path;
    for (; *path; path++)
        if (*path == '/' || *path == '\\')
            base = path + 1;
    return base;
}

/* A fixed data port, not one bound per transfer: not RFC, but clients follow PASV. */
static void cmd_pasv(struct platform *plt, s32 ctrl)
{
    u32 ip = local_ip(plt, ctrl);
    char response[80];

    snprintf(response, sizeof(response),
             "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
             ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF,
             FTP_DATA_PORT >> 8, FTP_DATA_PORT & 0xFF);
    reply(plt, ctrl, response);
}

static void cmd_stor(struct platform *plt, s32 ctrl, s32 data_listen,
                     const char *remote_path)
{
    char path[128];
    s32 file_fd, data_fd;
    u64 received = 0;

    snprintf(path, sizeof(path), "%s%s", FTP_DEST, basename_of(remote_path));

    file_fd = (s32)NC(plt->gadget, plt->kopen, (u64)path,
                      O_WRONLY_ | O_CREAT_ | O_TRUNC_, 0777, 0, 0, 0);
    if (file_fd < 0)
    {
        reply(plt, ctrl, "550 Cannot create file\r\n");
        return;
    }

    reply(plt, ctrl, "150 Opening data connection\r\n");
    data_fd = sock_accept(plt, data_listen);
    if (data_fd < 0)
    {
        NC(plt->gadget, plt->kclose, (u64)file_fd, 0, 0, 0, 0, 0);
        reply(plt, ctrl, "425 Cannot open data connection\r\n");
        return;
    }

    for (;;)
    {
        s32 n = sock_recv(plt, data_fd, transfer_buf, FTP_BUF_SIZE);
        if (n <= 0) break;
        if ((s32)NC(plt->gadget, plt->kwrite, (u64)file_fd, (u64)transfer_buf,
                    (u64)n, 0, 0, 0) != n)
            break;
        received += n;
    }

    sock_close(plt, data_fd);
    NC(plt->gadget, plt->kclose, (u64)file_fd, 0, 0, 0, 0, 0);
    reply(plt, ctrl, "226 Transfer complete\r\n");

    files_received++;
    bytes_received += received;
    snprintf(last_file, sizeof(last_file), "%s", basename_of(remote_path));
    remember_upload(last_file);
    printf("[FTP] received %s (%llu bytes)\n", last_file, received);
}

static void cmd_list(struct platform *plt, s32 ctrl, s32 data_listen)
{
    s32 data_fd;
    struct wad_choice found[WADMENU_MAX];
    int n;

    reply(plt, ctrl, "150 Opening data connection\r\n");
    data_fd = sock_accept(plt, data_listen);
    if (data_fd < 0)
    {
        reply(plt, ctrl, "425 Cannot open data connection\r\n");
        return;
    }

    n = dg_wad_scan(plt, found, WADMENU_MAX);
    for (int i = 0; i < n; i++)
    {
        char line[128];
        snprintf(line, sizeof(line),
                 "-rw-r--r-- 1 ps5 ps5 0 Jan 1 00:00 %s\r\n", found[i].filename);
        sock_send(plt, data_fd, line, (int)strlen(line));
    }

    sock_close(plt, data_fd);
    reply(plt, ctrl, "226 Transfer complete\r\n");
}

static void serve_client(struct platform *plt, s32 ctrl, s32 data_listen)
{
    char line[256];

    reply(plt, ctrl, "220 DOOM PS5 ready\r\n");

    for (;;)
    {
        int line_len = 0;
        char c;

        for (;;)
        {
            s32 got = sock_recv(plt, ctrl, &c, 1);
            if (got <= 0) return;
            if (c == '\n') break;
            if (c != '\r' && line_len < (int)sizeof(line) - 1)
                line[line_len++] = c;
        }
        line[line_len] = 0;
        if (line_len == 0) continue;

        char *arg = 0;
        for (int i = 0; line[i]; i++)
            if (line[i] == ' ')
        {
                line[i] = 0;
                arg = &line[i + 1];
                break;
            }

        if      (starts_with(line, "USER")) reply(plt, ctrl, "230 Logged in\r\n");
        else if (starts_with(line, "PASS")) reply(plt, ctrl, "230 Logged in\r\n");
        else if (starts_with(line, "SYST")) reply(plt, ctrl, "215 UNIX Type: L8\r\n");
        else if (starts_with(line, "TYPE")) reply(plt, ctrl, "200 Type set\r\n");
        else if (starts_with(line, "PWD"))  reply(plt, ctrl, "257 \"/\"\r\n");
        else if (starts_with(line, "CWD"))  reply(plt, ctrl, "250 OK\r\n");
        else if (starts_with(line, "NOOP")) reply(plt, ctrl, "200 OK\r\n");
        else if (starts_with(line, "FEAT")) reply(plt, ctrl, "211 End\r\n");
        else if (starts_with(line, "PASV")) cmd_pasv(plt, ctrl);
        else if (starts_with(line, "STOR") && arg)
            cmd_stor(plt, ctrl, data_listen, arg);
        else if (starts_with(line, "LIST") || starts_with(line, "NLST"))
            cmd_list(plt, ctrl, data_listen);

        else if (starts_with(line, "SITE") && arg && starts_with(arg, "EXIT"))
        {
            reply(plt, ctrl, "200 Closing server\r\n");
            should_exit = 1;
            return;
        }
        else if (starts_with(line, "QUIT"))
        {
            reply(plt, ctrl, "221 Bye\r\n");
            return;
        }
        else reply(plt, ctrl, "502 Not implemented\r\n");
    }
}

int dg_ftp_serve(struct platform *plt)
{
    struct dg_pad_state pad = { 0, 1 };
    s32 ctrl_listen, data_listen;

    files_received = 0;
    bytes_received = 0;
    last_file[0] = 0;
    should_exit = 0;

    dg_status_screen_reset();

    ctrl_listen = plt->ftp_srv_fd;
    data_listen = plt->ftp_data_fd;

    if (ctrl_listen < 0 || data_listen < 0)
    {
        printf("[FTP] launcher gave no sockets (ctrl=%d data=%d)\n",
               ctrl_listen, data_listen);
        for (int i = 0; i < FTP_UNAVAIL_FRAMES; i++)
            dg_wad_splash(plt, "FTP UNAVAILABLE");
        return 0;
    }

    printf("[FTP] listening on %d, data %d, uploads to %s\n",
           FTP_PORT, FTP_DATA_PORT, FTP_DEST);

    for (;;)
    {
        char title[64], status[64];

        snprintf(title, sizeof(title), "FTP READY ON PORT %d", FTP_PORT);
        if (files_received)
            snprintf(status, sizeof(status), "%d FILE%s - LAST %s",
                     files_received, files_received == 1 ? "" : "S", last_file);
        else
            snprintf(status, sizeof(status), "WAITING FOR UPLOAD");

        dg_status_screen(plt, title, status);

        if (sock_readable(plt, ctrl_listen))
        {
            s32 client_fd = sock_accept(plt, ctrl_listen);
            if (client_fd >= 0)
            {
                serve_client(plt, client_fd, data_listen);
                sock_close(plt, client_fd);
            }
        }

        if (should_exit) break;

        if (dg_pad_pressed(plt, &pad) & DS_CIRCLE) break;
    }

    printf("[FTP] stopped, %d file(s), %llu bytes\n",
           files_received, bytes_received);
    return files_received;
}
