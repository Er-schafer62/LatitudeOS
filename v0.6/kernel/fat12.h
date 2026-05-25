#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>

/* ── FAT12 Bios Parameter Block (BPB) ───────────────────────────────────── */

typedef struct {
    uint8_t  jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed)) FAT12_BPB;

/* ── FAT12 Directory Entry (32 bytes) ───────────────────────────────────── */

typedef struct {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) FAT12_DirEntry;

/* Attribute flags */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

/* Special first-byte values */
#define FAT_ENTRY_FREE      0xE5
#define FAT_ENTRY_END       0x00

/* FAT12 cluster sentinels */
#define FAT12_EOC           0xFF8
#define FAT12_BAD           0xFF7
#define FAT12_FREE          0x000

/* ── Result codes ───────────────────────────────────────────────────────── */

#define FAT12_OK            0
#define FAT12_ERR_DISK     -1
#define FAT12_ERR_NOTFOUND -2
#define FAT12_ERR_NOMEM    -3
#define FAT12_ERR_CORRUPT  -4

/* ── File info structure ────────────────────────────────────────────────── */

typedef struct {
    char     name[9];
    char     ext[4];
    uint8_t  attributes;
    uint32_t file_size;
    uint16_t first_cluster;
} FAT12_FileInfo;

/* ── Public API — read ──────────────────────────────────────────────────── */

int  fat12_init(uint32_t partition_lba);
void fat12_list_root(void (*callback)(const FAT12_FileInfo* info));
int  fat12_find(const char* filename, FAT12_FileInfo* out);
int  fat12_read_file(const FAT12_FileInfo* info, uint8_t* buf, uint32_t buf_size);
int  fat12_load(const char* filename, uint8_t* buf, uint32_t buf_size);

/* ── Public API — write ─────────────────────────────────────────────────── */

/* Create or overwrite a file with the given data.
   Changes are written to the in-RAM ramdisk copy (lost on poweroff).
   Returns FAT12_OK, FAT12_ERR_NOMEM (disk/dir full), or FAT12_ERR_DISK. */
int fat12_write_file(const char* filename, const uint8_t* buf, uint32_t size);

/* Delete a file from the root directory and free its clusters.
   Returns FAT12_OK or FAT12_ERR_NOTFOUND. */
int fat12_delete_file(const char* filename);

#endif