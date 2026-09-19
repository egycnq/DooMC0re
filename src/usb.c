#include "usb.h"

static void usb_log(struct usb_ctx *ctx, const char *msg)
{
    if (ctx->log_fd < 0 || !ctx->sendto) return;
    int len = 0;
    while (msg[len]) len++;
    NC(ctx->gadget, ctx->sendto, (u64)ctx->log_fd, (u64)msg, (u64)len, 0,
       (u64)ctx->log_sa, 16);
}

static void usb_log_str(struct usb_ctx *ctx, const char *prefix,
                        const char *text)
{
    char buf[128];
    int len = 0;
    while (*prefix && len < 90) buf[len++] = *prefix++;
    while (*text && len < 126) buf[len++] = *text++;
    buf[len++] = '\n';
    buf[len] = 0;
    usb_log(ctx, buf);
}

static void usb_log_num(struct usb_ctx *ctx, const char *prefix, u32 val)
{
    char buf[64];
    int len = 0;
    while (*prefix) buf[len++] = *prefix++;
    if (val == 0)
    {
        buf[len++] = '0';
    }
    else
    {
        char digits[12];
        int ndigits = 0;
        u32 rest = val;
        while (rest)
        {
            digits[ndigits++] = '0' + (rest % 10);
            rest /= 10;
        }
        while (ndigits > 0) buf[len++] = digits[--ndigits];
    }
    buf[len++] = '\n';
    buf[len] = 0;
    usb_log(ctx, buf);
}

static int usb_icase_cmp(const char *left, const char *right, int len)
{
    for (int i = 0; i < len; i++)
    {
        char left_ch  = left[i];
        char right_ch = right[i];
        if (left_ch >= 'A' && left_ch <= 'Z') left_ch += 32;
        if (right_ch >= 'A' && right_ch <= 'Z') right_ch += 32;
        if (left_ch != right_ch) return 1;
    }
    return 0;
}

static int usb_is_wad_name(const char *name, int len)
{
    if (len < 4) return 0;
    char dot  = name[len-4];
    char ext0 = name[len-3];
    char ext1 = name[len-2];
    char ext2 = name[len-1];
    if (dot != '.') return 0;
    if (ext0 >= 'A' && ext0 <= 'Z') ext0 += 32;
    if (ext1 >= 'A' && ext1 <= 'Z') ext1 += 32;
    if (ext2 >= 'A' && ext2 <= 'Z') ext2 += 32;
    return (ext0 == 'w' && ext1 == 'a' && ext2 == 'd');
}

static void usb_register_wad(struct usb_ctx *ctx, const char *name,
                             int name_len)
{
    if (!ctx->wads || ctx->wad_count >= ctx->max_wads) return;
    struct wad_entry *entry = &ctx->wads[ctx->wad_count];
    int n = 0;
    for (int j = 0; j < name_len && n < MAX_WAD_NAME - 1; j++)
        entry->filename[n++] = name[j];
    entry->filename[n] = 0;
    ctx->wad_count++;
}

static int scsi_send_cbw(struct usb_ctx *ctx, u32 data_len, u8 flags,
                         const u8 *cmd, u8 cmd_len)
{
    u8 *cbw = ctx->cbw;
    for (int i = 0; i < CBW_SIZE; i++) cbw[i] = 0;
    *(u32*)(cbw + 0) = CBW_SIG;
    *(u32*)(cbw + 4) = ctx->cbw_tag++;
    *(u32*)(cbw + 8) = data_len;
    cbw[12] = flags;
    cbw[14] = cmd_len;
    for (int i = 0; i < cmd_len && i < 16; i++) cbw[15 + i] = cmd[i];
    *(u32*)ctx->actual_len = 0;
    return (s32)NC(ctx->gadget, ctx->usb_bulk, ctx->handle, (u64)ctx->ep_out,
                   (u64)cbw, CBW_SIZE, (u64)ctx->actual_len, USB_TIMEOUT);
}

static int scsi_recv_data(struct usb_ctx *ctx, u8 *buf, u32 len)
{
    *(u32*)ctx->actual_len = 0;
    return (s32)NC(ctx->gadget, ctx->usb_bulk, ctx->handle, (u64)ctx->ep_in,
                   (u64)buf, (u64)len, (u64)ctx->actual_len, USB_TIMEOUT);
}

static int scsi_recv_csw(struct usb_ctx *ctx)
{
    for (int i = 0; i < CSW_SIZE; i++) ctx->csw[i] = 0;
    *(u32*)ctx->actual_len = 0;
    NC(ctx->gadget, ctx->usb_bulk, ctx->handle, (u64)ctx->ep_in, (u64)ctx->csw,
       CSW_SIZE, (u64)ctx->actual_len, USB_TIMEOUT);
    return ctx->csw[12];
}

static int read_sectors(struct usb_ctx *ctx, u32 lba, u32 count, u8 *buf)
{
    u8 cmd[10];
    cmd[0] = SCSI_READ10;
    cmd[1] = 0;
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8)  & 0xFF;
    cmd[5] = lba & 0xFF;
    cmd[6] = 0;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = count & 0xFF;
    cmd[9] = 0;
    u32 data_len = count * SECTOR_SIZE;
    if (scsi_send_cbw(ctx, data_len, CBW_FLAG_DATA_IN, cmd, 10) != 0) return -1;
    if (scsi_recv_data(ctx, buf, data_len) != 0) return -2;
    scsi_recv_csw(ctx);
    return 0;
}

static int scsi_inquiry(struct usb_ctx *ctx, u8 *buf)
{
    u8 cmd[6] = {SCSI_INQUIRY, 0, 0, 0, SCSI_INQUIRY_LEN, 0};
    if (scsi_send_cbw(ctx, SCSI_INQUIRY_LEN, CBW_FLAG_DATA_IN, cmd, 6) != 0)
        return -1;
    if (scsi_recv_data(ctx, buf, SCSI_INQUIRY_LEN) != 0) return -2;
    scsi_recv_csw(ctx);
    return 0;
}

static int discover_endpoints(struct usb_ctx *ctx)
{
    ctx->ep_in  = 0;
    ctx->ep_out = 0;

    u8 *desc = ctx->sector_buf;
    int total_len = 0;

    if (ctx->usb_get_cfg && ctx->usb_dev)
    {
        u64 cfg_ptr = 0;
        if ((s32)NC(ctx->gadget, ctx->usb_get_cfg, ctx->usb_dev, 0,
                    (u64)&cfg_ptr, 0,0,0) == 0 && cfg_ptr)
        {
            total_len = *(u16*)((u8*)cfg_ptr + 2);
            if (total_len > 0 && total_len <= SECTOR_SIZE)
            {
                for (int i = 0; i < total_len; i++) desc[i] = ((u8*)cfg_ptr)[i];
            }
            else total_len = 0;
        }
    }

    if (total_len > 0)
    {
        int offset = 0;
        u8 in_mass_storage = 0;
        while (offset < total_len - 1)
        {
            u8 desc_len  = desc[offset];
            u8 desc_type = desc[offset + 1];
            if (desc_len < 2) break;
            if (desc_type == USB_DESC_INTERFACE && desc_len >= 9)
            {
                in_mass_storage = (desc[offset + 5] == USB_CLASS_MASS_STORAGE);
            }
            if (desc_type == USB_DESC_ENDPOINT && desc_len >= 7 &&
                in_mass_storage)
            {
                u8 ep_addr = desc[offset + 2];
                u8 ep_attr = desc[offset + 3];
                if ((ep_attr & USB_EP_TYPE_MASK) == USB_EP_TYPE_BULK)
                {
                    if (ep_addr & USB_EP_DIR_IN)
                    {
                        ctx->ep_in = ep_addr;
                        ctx->max_pkt = *(u16*)(desc + offset + 4);
                    }
                    else
                    {
                        ctx->ep_out = ep_addr;
                    }
                }
            }
            offset += desc_len;
        }
    }

    if (ctx->ep_in && ctx->ep_out)
    {
        usb_log(ctx, "EP discovered\n");
        return 0;
    }

    u8 ep_probes[][2] = {{0x02, 0x81}, {0x01, 0x81}, {0x02, 0x82}, {0x01, 0x82}};
    u8 inquiry[SCSI_INQUIRY_LEN];
    for (int i = 0; i < 4; i++)
    {
        ctx->ep_out = ep_probes[i][0];
        ctx->ep_in  = ep_probes[i][1];
        if (scsi_inquiry(ctx, inquiry) == 0 && (inquiry[0] & 0x1F) == 0x00)
        {
            usb_log(ctx, "EP probed\n");
            return 0;
        }
    }

    ctx->ep_out = 0x02;
    ctx->ep_in  = 0x81;
    usb_log(ctx, "EP default 02/81\n");
    return 0;
}

static int detect_partition(struct usb_ctx *ctx)
{
    if (read_sectors(ctx, 0, 1, ctx->sector_buf) != 0) return -1;

    if (ctx->sector_buf[510] != 0x55 || ctx->sector_buf[511] != 0xAA)
    {
        usb_log(ctx, "No MBR\n");
        return -1;
    }

    for (int i = 0; i < MBR_ENTRY_COUNT; i++)
    {
        u8 *entry = ctx->sector_buf + MBR_TABLE_OFF + i * MBR_ENTRY_SIZE;
        u8  part_type = entry[4];
        u32 lba       = *(u32*)(entry + 8);
        if (lba == 0) continue;

        if (part_type == MBR_TYPE_FAT32 || part_type == MBR_TYPE_FAT32_LBA)
        {
            ctx->part_lba = lba;
            ctx->fs_type = FS_FAT32;
            usb_log_num(ctx, "FAT32 at LBA ", lba);
            return 0;
        }
        if (part_type == MBR_TYPE_NTFS_EXFAT)
        {
            if (read_sectors(ctx, lba, 1, ctx->sector_buf) != 0) continue;
            if (ctx->sector_buf[3]=='E' && ctx->sector_buf[4]=='X' &&
                ctx->sector_buf[5]=='F' && ctx->sector_buf[6]=='A' &&
                ctx->sector_buf[7]=='T')
            {
                ctx->part_lba = lba;
                ctx->fs_type = FS_EXFAT;
                usb_log_num(ctx, "exFAT at LBA ", lba);
                return 0;
            }
            ctx->part_lba = lba;
            ctx->fs_type = FS_FAT32;
            usb_log_num(ctx, "Type07 as FAT32 LBA ", lba);
            return 0;
        }
    }

    usb_log(ctx, "No partition\n");
    return -1;
}

static int fat32_parse_bpb(struct usb_ctx *ctx)
{
    if (read_sectors(ctx, ctx->part_lba, 1, ctx->sector_buf) != 0) return -1;
    u8 *bpb = ctx->sector_buf;

    ctx->bytes_per_sector = *(u16*)(bpb + 11);
    ctx->sectors_per_cluster = bpb[13];
    u16 reserved    = *(u16*)(bpb + 14);
    u8  fat_count   = bpb[16];
    u32 fat_sectors = *(u32*)(bpb + 36);
    ctx->root_cluster = *(u32*)(bpb + 44);
    ctx->cluster_size = ctx->bytes_per_sector * ctx->sectors_per_cluster;
    ctx->fat_lba  = ctx->part_lba + reserved;
    ctx->data_lba = ctx->fat_lba + fat_count * fat_sectors;
    ctx->total_clusters = (*(u32*)(bpb + 32) - reserved -
                           fat_count * fat_sectors) / ctx->sectors_per_cluster;

    usb_log_num(ctx, "cluster_size=", ctx->cluster_size);
    usb_log_num(ctx, "root_cluster=", ctx->root_cluster);
    return 0;
}

static u32 cluster_to_lba(struct usb_ctx *ctx, u32 cluster)
{
    return ctx->data_lba + (cluster - 2) * ctx->sectors_per_cluster;
}

static u32 fat_next_cluster(struct usb_ctx *ctx, u32 cluster)
{
    u32 fat_offset = cluster * 4;
    u32 fat_sector = ctx->fat_lba + fat_offset / SECTOR_SIZE;
    u32 entry_offset = fat_offset % SECTOR_SIZE;
    if (read_sectors(ctx, fat_sector, 1, ctx->sector_buf) != 0)
        return 0xFFFFFFFF;
    u32 val = *(u32*)(ctx->sector_buf + entry_offset);
    if (ctx->fs_type == FS_FAT32) val &= 0x0FFFFFFF;
    return val;
}

static int read_cluster(struct usb_ctx *ctx, u32 cluster, u8 *buf)
{
    return read_sectors(ctx, cluster_to_lba(ctx, cluster),
                        ctx->sectors_per_cluster, buf);
}

static int exfat_parse_vbr(struct usb_ctx *ctx)
{
    if (read_sectors(ctx, ctx->part_lba, 1, ctx->sector_buf) != 0) return -1;
    u8 *vbr = ctx->sector_buf;

    u8 sector_shift  = vbr[108];
    u8 cluster_shift = vbr[109];
    ctx->bytes_per_sector = 1 << sector_shift;
    ctx->sectors_per_cluster = 1 << cluster_shift;
    ctx->cluster_size = ctx->bytes_per_sector * ctx->sectors_per_cluster;
    u32 fat_offset  = *(u32*)(vbr + 80);
    u32 heap_offset = *(u32*)(vbr + 88);
    ctx->root_cluster = *(u32*)(vbr + 96);
    ctx->total_clusters = *(u32*)(vbr + 92);
    ctx->fat_lba  = ctx->part_lba + fat_offset;
    ctx->data_lba = ctx->part_lba + heap_offset;

    usb_log_num(ctx, "exFAT cluster_size=", ctx->cluster_size);
    usb_log_num(ctx, "exFAT root_cluster=", ctx->root_cluster);
    return 0;
}

static void lfn_extract(u8 *entry, char *name, int pos)
{
    int offsets[] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};
    for (int j = 0; j < LFN_CHARS_PER_ENTRY; j++)
    {
        int idx = pos + j;
        if (idx >= 255) break;
        u16 ch = *(u16*)(entry + offsets[j]);
        if (ch == 0 || ch == 0xFFFF) break;
        name[idx] = (ch < 128) ? (char)ch : '_';
    }
}

static u32 fat32_find_folder(struct usb_ctx *ctx, u32 dir_cluster,
                             const char *name, int name_len)
{
    u32 cluster = dir_cluster;
    u32 eoc = (ctx->fs_type == FS_FAT32) ? FAT32_EOC : EXFAT_EOC;

    char lfn[256];
    int lfn_active = 0;

    while (cluster >= 2 && cluster < eoc)
    {
        if (read_cluster(ctx, cluster, ctx->cluster_buf) != 0) return 0;
        int entries = ctx->cluster_size / DIR_ENTRY_SIZE;
        for (int i = 0; i < entries; i++)
        {
            u8 *entry = ctx->cluster_buf + i * DIR_ENTRY_SIZE;
            if (entry[0] == FAT32_ENTRY_END) return 0;
            if (entry[0] == FAT32_ENTRY_FREE)
            {
                lfn_active = 0;
                continue;
            }
            u8 attr = entry[0x0B];

            if (attr == FAT32_ATTR_LFN)
            {
                u8 seq = entry[0];
                if (seq & FAT32_LFN_LAST)
                {
                    for (int j = 0; j < 256; j++) lfn[j] = 0;
                    lfn_active = 1;
                }
                if (lfn_active)
                {
                    int part_index = (seq & FAT32_LFN_SEQ_MASK) - 1;
                    lfn_extract(entry, lfn, part_index * LFN_CHARS_PER_ENTRY);
                }
                continue;
            }

            if (!(attr & FAT32_ATTR_DIR))
            {
                lfn_active = 0;
                continue;
            }
            if (attr & FAT32_ATTR_VOLID)
            {
                lfn_active = 0;
                continue;
            }

            char short_name[12];
            int short_len = 0;
            for (int j = 0; j < 8 && entry[j] != ' '; j++)
                short_name[short_len++] = entry[j];
            short_name[short_len] = 0;

            int match = 0;
            if (short_len == name_len &&
                usb_icase_cmp(short_name, name, name_len) == 0) match = 1;
            if (!match && lfn_active && lfn[0])
            {
                int lfn_len = 0;
                while (lfn[lfn_len]) lfn_len++;
                if (lfn_len == name_len &&
                    usb_icase_cmp(lfn, name, name_len) == 0) match = 1;
            }
            lfn_active = 0;

            if (match)
            {
                u32 cluster_hi = *(u16*)(entry + 0x14);
                u32 cluster_lo = *(u16*)(entry + 0x1A);
                return (cluster_hi << 16) | cluster_lo;
            }
        }
        cluster = fat_next_cluster(ctx, cluster);
    }
    return 0;
}

static int fat32_copy_wad_files(struct usb_ctx *ctx, u32 dir_cluster)
{
    int copied = 0;
    u32 cluster = dir_cluster;
    u32 eoc = (ctx->fs_type == FS_FAT32) ? FAT32_EOC : EXFAT_EOC;
    u8 *dir = (u8 *)NC(ctx->gadget, ctx->mmap, 0, (u64)ctx->cluster_size, 3,
                       0x1002, (u64)-1, 0);
    if ((s64)dir == -1) return 0;

    char lfn[256];
    int lfn_active = 0;

    while (cluster >= 2 && cluster < eoc)
    {
        if (read_cluster(ctx, cluster, dir) != 0) break;
        int entries = ctx->cluster_size / DIR_ENTRY_SIZE;
        for (int i = 0; i < entries; i++)
        {
            u8 *entry = dir + i * DIR_ENTRY_SIZE;
            if (entry[0] == FAT32_ENTRY_END) goto dir_done;
            if (entry[0] == FAT32_ENTRY_FREE)
            {
                lfn_active = 0;
                continue;
            }
            u8 attr = entry[0x0B];

            if (attr == FAT32_ATTR_LFN)
            {
                u8 seq = entry[0];
                if (seq & FAT32_LFN_LAST)
                {
                    for (int j = 0; j < 256; j++) lfn[j] = 0;
                    lfn_active = 1;
                }
                if (lfn_active)
                {
                    int part_index = (seq & FAT32_LFN_SEQ_MASK) - 1;
                    lfn_extract(entry, lfn, part_index * LFN_CHARS_PER_ENTRY);
                }
                continue;
            }

            if ((attr & FAT32_ATTR_DIR) || (attr & FAT32_ATTR_VOLID))
            {
                lfn_active = 0;
                continue;
            }

            char *name;
            int name_len;
            char short_name[13];
            int short_len = 0;
            for (int j = 0; j < 8 && entry[j] != ' '; j++)
                short_name[short_len++] = entry[j];
            short_name[short_len++] = '.';
            for (int j = 0; j < 3 && entry[8+j] != ' '; j++)
                short_name[short_len++] = entry[8+j];
            short_name[short_len] = 0;

            if (lfn_active && lfn[0])
            {
                name = lfn;
                name_len = 0;
                while (lfn[name_len]) name_len++;
            }
            else
            {
                name = short_name;
                name_len = short_len;
            }
            lfn_active = 0;

            if (!usb_is_wad_name(name, name_len)) continue;

            u32 first_cluster = ((u32)*(u16*)(entry + 0x14) << 16) |
                                *(u16*)(entry + 0x1A);
            u32 file_size = *(u32*)(entry + 0x1C);
            if (first_cluster < 2 || file_size == 0) continue;

            char path[96];
            int path_len = 0;
            const char *prefix = WAD_DEST_DIR;
            while (*prefix) path[path_len++] = *prefix++;
            /* 8.3 names come back uppercase; the PS5 filesystem is
               case-sensitive and find_wad() probes lowercase only. */
            for (int j = 0; j < name_len && path_len < 94; j++)
            {
                char ch = name[j];
                if (ch >= 'A' && ch <= 'Z') ch += 32;
                path[path_len++] = ch;
            }
            path[path_len] = 0;

            s32 fd = (s32)NC(ctx->gadget, ctx->kopen, (u64)path,
                             (u64)(O_WRONLY | O_CREAT | O_EXCL),
                             (u64)0666, 0, 0, 0);
            if (fd < 0)
            {
                usb_register_wad(ctx, name, name_len);
                copied++;
                continue;
            }

            u32 remain = file_size;
            u32 file_cluster = first_cluster;
            int ok = 1;
            while (remain > 0 && file_cluster >= 2 && file_cluster < eoc)
            {
                if (read_cluster(ctx, file_cluster, ctx->cluster_buf) != 0)
                {
                    ok = 0;
                    break;
                }
                u32 chunk = remain < ctx->cluster_size ?
                            remain : ctx->cluster_size;
                NC(ctx->gadget, ctx->kwrite, (u64)fd, (u64)ctx->cluster_buf,
                   (u64)chunk, 0, 0, 0);
                remain -= chunk;
                if (remain > 0)
                    file_cluster = fat_next_cluster(ctx, file_cluster);
            }
            NC(ctx->gadget, ctx->kclose, (u64)fd, 0,0,0,0,0);

            if (ok)
            {
                usb_log_str(ctx, "  + ", name);
                usb_register_wad(ctx, name, name_len);
                copied++;
            }
            else
            {
                usb_log_str(ctx, "  ERR ", name);
            }
        }
        cluster = fat_next_cluster(ctx, cluster);
    }
dir_done:
    return copied;
}

static u32 exfat_find_folder(struct usb_ctx *ctx, u32 dir_cluster)
{
    u32 cluster = dir_cluster;
    u8 *dir = (u8 *)NC(ctx->gadget, ctx->mmap, 0, (u64)ctx->cluster_size, 3,
                       0x1002, (u64)-1, 0);
    if ((s64)dir == -1) return 0;

    u32 found_cluster = 0;
    u8  found_attr = 0;
    int expecting_stream = 0;
    int expecting_name = 0;
    u8  name_len = 0;

    while (cluster >= 2 && cluster < EXFAT_EOC)
    {
        if (read_cluster(ctx, cluster, dir) != 0) break;
        int entries = ctx->cluster_size / DIR_ENTRY_SIZE;
        for (int i = 0; i < entries; i++)
        {
            u8 *entry = dir + i * DIR_ENTRY_SIZE;
            u8 entry_type = entry[0];
            if (entry_type == EXFAT_ENTRY_EOD) goto exf_done;

            if (entry_type == EXFAT_ENTRY_FILE)
            {
                found_attr = *(u16*)(entry + 4) & 0xFF;
                expecting_stream = 1;
                expecting_name = 0;
            }
            else if (entry_type == EXFAT_ENTRY_STREAM && expecting_stream)
            {
                expecting_stream = 0;
                name_len = entry[3];
                found_cluster = *(u32*)(entry + 20);
                if ((found_attr & EXFAT_ATTR_DIR) && name_len == 4)
                    expecting_name = 1;
                else
                    expecting_name = 0;
            }
            else if (entry_type == EXFAT_ENTRY_FNAME && expecting_name)
            {
                expecting_name = 0;
                char name[5];
                name[0] = entry[2];
                name[1] = entry[4];
                name[2] = entry[6];
                name[3] = entry[8];
                name[4] = 0;
                if (usb_icase_cmp(name, "doom", 4) == 0) goto exf_done;
                found_cluster = 0;
            }
            else
            {
                expecting_stream = 0;
                expecting_name = 0;
            }
        }
        cluster = fat_next_cluster(ctx, cluster);
    }
exf_done:
    return found_cluster;
}

static int exfat_copy_wad_files(struct usb_ctx *ctx, u32 dir_cluster)
{
    int copied = 0;
    u32 cluster = dir_cluster;
    u8 *dir = (u8 *)NC(ctx->gadget, ctx->mmap, 0, (u64)ctx->cluster_size, 3,
                       0x1002, (u64)-1, 0);
    if ((s64)dir == -1) return 0;

    char cur_name[64];
    int  cur_name_len = 0;
    u32  cur_cluster = 0;
    u64  cur_size = 0;
    u8   cur_attr = 0;
    u8   no_fat_chain = 0;
    int  phase = 0;

    while (cluster >= 2 && cluster < EXFAT_EOC)
    {
        if (read_cluster(ctx, cluster, dir) != 0) break;
        int entries = ctx->cluster_size / DIR_ENTRY_SIZE;
        for (int i = 0; i < entries; i++)
        {
            u8 *entry = dir + i * DIR_ENTRY_SIZE;
            u8 entry_type = entry[0];
            if (entry_type == EXFAT_ENTRY_EOD) goto exf2_done;

            if (entry_type == EXFAT_ENTRY_FILE)
            {
                cur_attr = *(u16*)(entry + 4) & 0xFF;
                cur_name_len = 0;
                cur_name[0] = 0;
                phase = 1;
            }
            else if (entry_type == EXFAT_ENTRY_STREAM && phase == 1)
            {
                u8 gen_flags = entry[1];
                /* NoFatChain: clusters are contiguous and the FAT holds nothing
                   for this file, so the cluster number is stepped instead. */
                no_fat_chain = (gen_flags & EXFAT_FLAG_NO_FAT_CHAIN) ? 1 : 0;
                u8 name_len = entry[3];
                cur_cluster = *(u32*)(entry + 20);
                cur_size = *(u64*)(entry + 24);
                cur_name_len = 0;
                (void)name_len;
                phase = 2;
            }
            else if (entry_type == EXFAT_ENTRY_FNAME && phase == 2)
            {
                for (int j = 0; j < 15 && cur_name_len < 60; j++)
                {
                    u16 ch = *(u16*)(entry + 2 + j * 2);
                    if (ch == 0) break;
                    cur_name[cur_name_len++] = (ch < 128) ? (char)ch : '_';
                }
                cur_name[cur_name_len] = 0;
            }
            else if (phase == 2 && entry_type != EXFAT_ENTRY_FNAME)
            {
                if (!(cur_attr & EXFAT_ATTR_DIR) && cur_size > 0 &&
                    cur_cluster >= 2 && usb_is_wad_name(cur_name, cur_name_len))
                {

                    char path[96];
                    int path_len = 0;
                    const char *prefix = WAD_DEST_DIR;
                    while (*prefix) path[path_len++] = *prefix++;
                    for (int j = 0; j < cur_name_len && j < 47; j++)
                    {
                        char ch = cur_name[j];
                        if (ch >= 'A' && ch <= 'Z') ch += 32;
                        path[path_len++] = ch;
                    }
                    path[path_len] = 0;

                    s32 fd = (s32)NC(ctx->gadget, ctx->kopen, (u64)path,
                                     (u64)(O_WRONLY | O_CREAT | O_EXCL),
                                     (u64)0666, 0, 0, 0);
                    if (fd < 0)
                    {
                        usb_register_wad(ctx, cur_name, cur_name_len);
                        copied++;
                    }
                    else
                    {
                        u64 remain = cur_size;
                        u32 file_cluster = cur_cluster;
                        int ok = 1;
                        while (remain > 0 && file_cluster >= 2 &&
                               file_cluster < EXFAT_EOC)
                        {
                            if (read_cluster(ctx, file_cluster,
                                             ctx->cluster_buf) != 0)
                            {
                                ok = 0;
                                break;
                            }
                            u32 chunk = remain < ctx->cluster_size ?
                                        (u32)remain : ctx->cluster_size;
                            NC(ctx->gadget, ctx->kwrite, (u64)fd,
                               (u64)ctx->cluster_buf, (u64)chunk, 0, 0, 0);
                            remain -= chunk;
                            if (remain > 0)
                            {
                                if (no_fat_chain) file_cluster++;
                                else file_cluster =
                                    fat_next_cluster(ctx, file_cluster);
                            }
                        }
                        NC(ctx->gadget, ctx->kclose, (u64)fd, 0,0,0,0,0);
                        if (ok)
                        {
                            usb_log_str(ctx, "  + ", cur_name);
                            usb_register_wad(ctx, cur_name, cur_name_len);
                            copied++;
                        }
                        else
                        {
                            usb_log_str(ctx, "  ERR ", cur_name);
                        }
                    }
                }
                phase = 0;
                if (entry_type == EXFAT_ENTRY_FILE)
                {
                    cur_attr = *(u16*)(entry + 4) & 0xFF;
                    phase = 1;
                }
            }
        }
        cluster = fat_next_cluster(ctx, cluster);
    }
exf2_done:
    return copied;
}

static void clean_temp_dir(struct usb_ctx *ctx, void *getdents)
{
    if (!ctx->kopen || !ctx->kclose || !ctx->kunlink || !getdents) return;
    s32 dir_fd = (s32)NC(ctx->gadget, ctx->kopen, (u64)WAD_DEST_SUBDIR,
                         (u64)O_DIRECTORY, 0, 0, 0, 0);
    if (dir_fd < 0) return;

    u8 *dir_buf = ctx->sector_buf;
    for (;;)
    {
        s32 bytes = (s32)NC(ctx->gadget, getdents, (u64)dir_fd, (u64)dir_buf,
                            SECTOR_SIZE, 0, 0, 0);
        if (bytes <= 0) break;
        s32 offset = 0;
        while (offset < bytes)
        {
            u32 record_len = *(u16*)(dir_buf + offset + DIRENT_RECLEN_OFF);
            if (record_len == 0) break;
            char *name = (char *)(dir_buf + offset + DIRENT_NAME_OFF);
            int name_len = 0;
            while (name[name_len]) name_len++;
            if (usb_is_wad_name(name, name_len))
            {
                char path[96];
                int path_len = 0;
                const char *prefix = WAD_DEST_DIR;
                while (*prefix) path[path_len++] = *prefix++;
                for (int j = 0; j < name_len && j < 60; j++)
                    path[path_len++] = name[j];
                path[path_len] = 0;
                NC(ctx->gadget, ctx->kunlink, (u64)path, 0,0,0,0,0);
            }
            offset += record_len;
        }
    }
    NC(ctx->gadget, ctx->kclose, (u64)dir_fd, 0,0,0,0,0);
}

int usb_load_wads(void *gadget, void *dlsym_fn, void *load_mod, void *mmap_fn,
                  void *kopen, void *kwrite, void *kclose,
                  void *kunlink, void *kmkdir, void *getdents,
                  void *sendto, s32 log_fd, u8 *log_sa,
                  struct wad_entry *wads, int max_wads)
{

    struct usb_ctx usb_state;
    struct usb_ctx *ctx = &usb_state;
    u8 *raw = (u8*)ctx;
    for (u32 i = 0; i < sizeof(struct usb_ctx); i++) raw[i] = 0;

    ctx->gadget = gadget;
    ctx->dlsym_fn = dlsym_fn;
    ctx->kopen = kopen;
    ctx->kwrite = kwrite;
    ctx->kclose = kclose;
    ctx->kunlink = kunlink;
    ctx->kmkdir = kmkdir;
    ctx->mmap = mmap_fn;
    ctx->sendto = sendto;
    ctx->log_fd = log_fd;
    ctx->log_sa = log_sa;
    ctx->cbw_tag = 1;
    ctx->wads = wads;
    ctx->max_wads = max_wads;
    ctx->wad_count = 0;

    usb_log(ctx, "[USB] Init\n");

    s32 usbd_mod = (s32)NC(gadget, load_mod, (u64)"libSceUsbd.sprx", 0,0,0,0,0);
    if (usbd_mod <= 0 || (u32)usbd_mod >= 0x80000000)
    {
        usb_log(ctx, "[USB] No libSceUsbd\n");
        return 0;
    }

    ctx->usb_init      = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdInit");
    ctx->usb_exit      = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdExit");
    ctx->usb_get_list  = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdGetDeviceList");
    ctx->usb_free_list = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdFreeDeviceList");
    ctx->usb_get_desc  = SYM(gadget, dlsym_fn, usbd_mod,
                             "sceUsbdGetDeviceDescriptor");
    ctx->usb_get_cfg   = SYM(gadget, dlsym_fn, usbd_mod,
                             "sceUsbdGetConfigDescriptor");
    ctx->usb_open      = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdOpen");
    ctx->usb_close     = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdClose");
    ctx->usb_claim     = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdClaimInterface");
    ctx->usb_release   = SYM(gadget, dlsym_fn, usbd_mod,
                             "sceUsbdReleaseInterface");
    ctx->usb_detach    = SYM(gadget, dlsym_fn, usbd_mod,
                             "sceUsbdDetachKernelDriver");
    ctx->usb_bulk      = SYM(gadget, dlsym_fn, usbd_mod, "sceUsbdBulkTransfer");

    if (!ctx->usb_init || !ctx->usb_get_list || !ctx->usb_open || !ctx->usb_bulk)
    {
        usb_log(ctx, "[USB] Missing funcs\n");
        return 0;
    }

    ctx->cbw        = (u8 *)NC(gadget, mmap_fn, 0, 0x1000, 3, 0x1002, (u64)-1, 0);
    ctx->csw        = ctx->cbw + 64;
    ctx->actual_len = ctx->cbw + 128;
    ctx->sector_buf = (u8 *)NC(gadget, mmap_fn, 0, 0x1000, 3, 0x1002, (u64)-1, 0);

    if ((s64)ctx->cbw == -1 || (s64)ctx->sector_buf == -1)
    {
        usb_log(ctx, "[USB] mmap fail\n");
        return 0;
    }

    NC(gadget, ctx->usb_init, 0,0,0,0,0,0);

    u64 list_ptr = 0;
    s32 dev_count = (s32)NC(gadget, ctx->usb_get_list, (u64)&list_ptr, 0,0,0,0,0);
    if (dev_count <= 0 || list_ptr == 0)
    {
        usb_log(ctx, "[USB] No devices\n");
        NC(gadget, ctx->usb_exit, 0,0,0,0,0,0);
        return 0;
    }
    usb_log_num(ctx, "[USB] Devices: ", (u32)dev_count);

    u8 desc[18];
    u64 usb_dev = 0;
    for (int i = 0; i < dev_count; i++)
    {
        u64 dev_ptr = *(u64*)(list_ptr + i * 8);
        if (!dev_ptr) continue;
        for (int j = 0; j < 18; j++) desc[j] = 0;
        if ((s32)NC(gadget, ctx->usb_get_desc, dev_ptr, (u64)desc, 0,0,0,0) == 0)
        {
            u8 dev_class = desc[4];
            if (dev_class == USB_CLASS_MASS_STORAGE || dev_class == 0x00)
            {
                usb_dev = dev_ptr;
                break;
            }
        }
    }

    if (!usb_dev)
    {
        usb_log(ctx, "[USB] No mass storage\n");
        if (ctx->usb_free_list)
            NC(gadget, ctx->usb_free_list, list_ptr, 1, 0,0,0,0);
        NC(gadget, ctx->usb_exit, 0,0,0,0,0,0);
        return 0;
    }

    u64 handle = 0;
    ctx->usb_dev = usb_dev;
    NC(gadget, ctx->usb_open, usb_dev, (u64)&handle, 0,0,0,0);
    ctx->handle = handle;
    if (ctx->usb_detach) NC(gadget, ctx->usb_detach, handle, 0, 0,0,0,0);
    if ((s32)NC(gadget, ctx->usb_claim, handle, 0, 0,0,0,0) != 0)
    {
        usb_log(ctx, "[USB] Claim fail\n");
        NC(gadget, ctx->usb_close, handle, 0,0,0,0,0);
        NC(gadget, ctx->usb_exit, 0,0,0,0,0,0);
        return 0;
    }
    usb_log(ctx, "[USB] Drive open\n");

    discover_endpoints(ctx);

    if (detect_partition(ctx) != 0)
    {
        usb_log(ctx, "[USB] No valid partition\n");
        goto usb_cleanup;
    }

    if (ctx->fs_type == FS_FAT32)
    {
        if (fat32_parse_bpb(ctx) != 0) goto usb_cleanup;
    }
    else
    {
        if (exfat_parse_vbr(ctx) != 0) goto usb_cleanup;
    }

    ctx->cluster_buf = (u8 *)NC(gadget, mmap_fn, 0, (u64)ctx->cluster_size, 3,
                                0x1002, (u64)-1, 0);
    if ((s64)ctx->cluster_buf == -1)
    {
        usb_log(ctx, "[USB] clust mmap fail\n");
        goto usb_cleanup;
    }

    if (kmkdir) NC(gadget, kmkdir, (u64)WAD_DEST_SUBDIR, (u64)0777, 0,0,0,0);

    u32 wad_folder = 0;
    if (ctx->fs_type == FS_FAT32)
        wad_folder = fat32_find_folder(ctx, ctx->root_cluster, "doom", 4);
    else
        wad_folder = exfat_find_folder(ctx, ctx->root_cluster);

    if (wad_folder == 0)
    {
        usb_log(ctx, "[USB] No 'doom' folder\n");
        goto usb_cleanup;
    }
    usb_log_num(ctx, "[USB] doom folder cluster=", wad_folder);

    int copied = 0;
    if (ctx->fs_type == FS_FAT32)
        copied = fat32_copy_wad_files(ctx, wad_folder);
    else
        copied = exfat_copy_wad_files(ctx, wad_folder);

    usb_log_num(ctx, "[USB] Copied ", (u32)copied);
    ctx->files_copied = copied;

usb_cleanup:
    if (ctx->usb_release) NC(gadget, ctx->usb_release, handle, 0, 0,0,0,0);
    NC(gadget, ctx->usb_close, handle, 0,0,0,0,0);
    if (ctx->usb_free_list) NC(gadget, ctx->usb_free_list, list_ptr, 1, 0,0,0,0);
    NC(gadget, ctx->usb_exit, 0,0,0,0,0,0);
    usb_log(ctx, "[USB] Done\n");
    return ctx->wad_count;
}
