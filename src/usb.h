#ifndef USB_H
#define USB_H

#include "core.h"

#define MAX_WADS 16
#define MAX_WAD_NAME 48

struct wad_entry
{
    char filename[MAX_WAD_NAME];
};

#define USB_CLASS_MASS_STORAGE 0x08
#define USB_SC_SCSI            0x06
#define USB_PR_BOT             0x50

#define USB_DESC_INTERFACE 0x04
#define USB_DESC_ENDPOINT  0x05
#define USB_EP_TYPE_MASK   0x03
#define USB_EP_TYPE_BULK   0x02
#define USB_EP_DIR_IN      0x80

#define CBW_SIG  0x43425355
#define CSW_SIG  0x53425355
#define CBW_SIZE 31
#define CSW_SIZE 13

#define CBW_FLAG_DATA_IN 0x80

#define SCSI_INQUIRY       0x12
#define SCSI_READ_CAP10    0x25
#define SCSI_READ10        0x28
#define SCSI_INQUIRY_LEN   36

#define USB_TIMEOUT 5000
#define SECTOR_SIZE 512

#define O_WRONLY    0x0001
#define O_CREAT     0x0200
#define O_EXCL      0x0800
#define O_DIRECTORY 0x20000

#define FS_NONE   0
#define FS_FAT32  1
#define FS_EXFAT  2

#define WAD_DEST_DIR    "/av_contents/content_tmp/"
#define WAD_DEST_SUBDIR "/av_contents/content_tmp"

#define MBR_TABLE_OFF       0x1BE
#define MBR_ENTRY_SIZE      16
#define MBR_ENTRY_COUNT     4
#define MBR_TYPE_FAT32      0x0B
#define MBR_TYPE_FAT32_LBA  0x0C
#define MBR_TYPE_NTFS_EXFAT 0x07

#define DIR_ENTRY_SIZE 32

#define DIRENT_RECLEN_OFF 4
#define DIRENT_NAME_OFF   8

#define EXFAT_ENTRY_EOD       0x00
#define EXFAT_ENTRY_ALLOC_BMP 0x81
#define EXFAT_ENTRY_FILE      0x85
#define EXFAT_ENTRY_STREAM    0xC0
#define EXFAT_ENTRY_FNAME     0xC1
#define EXFAT_ENTRY_VLABEL    0x83
#define EXFAT_ENTRY_UPCASE    0x82

#define EXFAT_ATTR_DIR          0x10
#define EXFAT_FLAG_NO_FAT_CHAIN 0x02

#define FAT32_ATTR_LFN    0x0F
#define FAT32_ATTR_VOLID  0x08
#define FAT32_ATTR_DIR    0x10

#define FAT32_ENTRY_END  0x00
#define FAT32_ENTRY_FREE 0xE5

#define FAT32_LFN_LAST      0x40
#define FAT32_LFN_SEQ_MASK  0x1F
#define LFN_CHARS_PER_ENTRY 13

#define FAT32_EOC 0x0FFFFFF8
#define EXFAT_EOC 0xFFFFFFF8

struct usb_ctx
{
    void *gadget, *dlsym_fn;

    void *usb_init, *usb_exit;
    void *usb_get_list, *usb_free_list;
    void *usb_get_desc, *usb_get_cfg;
    void *usb_open, *usb_close;
    void *usb_claim, *usb_release, *usb_detach;
    void *usb_bulk;

    void *kopen, *kwrite, *kclose, *kunlink, *kmkdir;
    void *mmap, *sendto;

    s32 log_fd;
    u8 *log_sa;

    u64  handle;
    u64  usb_dev;
    u8   ep_in, ep_out;
    u16  max_pkt;
    u32  cbw_tag;

    u8  *cbw, *csw, *actual_len;
    u8  *sector_buf;
    u8  *cluster_buf;

    u8   fs_type;
    u32  part_lba;
    u32  bytes_per_sector;
    u32  sectors_per_cluster;
    u32  cluster_size;
    u32  fat_lba;
    u32  data_lba;
    u32  root_cluster;
    u32  total_clusters;

    u32  files_copied;
    u32  files_skipped;

    struct wad_entry *wads;
    int  max_wads;
    int  wad_count;
};

int usb_load_wads(void *gadget, void *dlsym_fn, void *load_mod, void *mmap_fn,
                  void *kopen, void *kwrite, void *kclose,
                  void *kunlink, void *kmkdir, void *getdents,
                  void *sendto, s32 log_fd, u8 *log_sa,
                  struct wad_entry *wads, int max_wads);

#endif
