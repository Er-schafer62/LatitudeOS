/* fat12.c — FAT12 filesystem driver
 *
 * Reads and writes a FAT12 volume loaded into RAM at 0x20000 by stage2.
 * Supports: init, root directory listing, file find, file read,
 *           file write (create/overwrite), file delete.
 *
 * Write note: changes are committed to the in-RAM ramdisk copy immediately,
 * so ls/cat see them right away. They are lost on poweroff — no ATA write
 * driver exists yet. Use 'netls'/'get' to pull files back from the server.
 *
 * FAT12 disk layout:
 *  [ Reserved sectors (BPB in sector 0) ]
 *  [ FAT 1 (sectors_per_fat sectors)    ]
 *  [ FAT 2 (sectors_per_fat sectors)    ]  <- kept in sync on writes
 *  [ Root directory (root_entry_count * 32 bytes, rounded up to sectors) ]
 *  [ Data area (clusters 2..N)          ]
 */

#include "fat12.h"
#include "disk.h"
#include <stdint.h>
#include <stddef.h>

/* ── Internal state ─────────────────────────────────────────────────────── */

static uint8_t  sector_buf[512];
static uint8_t  fat_buf[12 * 512];   /* FAT1 loaded in full */

static uint32_t g_partition_lba;
static uint32_t g_fat_lba;
static uint32_t g_root_dir_lba;
static uint32_t g_data_lba;
static uint32_t g_root_dir_sectors;
static uint8_t  g_sectors_per_cluster;
static uint16_t g_bytes_per_sector;
static uint16_t g_root_entry_count;
static uint8_t  g_fat_sectors;
static uint8_t  g_num_fats;

static int g_initialized = 0;

/* ── String / memory helpers (no stdlib in kernel) ──────────────────────── */

static int k_strlen(const char* s) {
    int i = 0; while (s[i]) i++; return i;
}

static void k_memset(void* dst, uint8_t val, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = val;
}

static void k_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static int k_toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* ── FAT12 entry read ───────────────────────────────────────────────────── */

/*
 * FAT12 packs two 12-bit values into every 3 bytes.
 *
 * For cluster N:
 *   byte_offset = (N * 3) / 2
 *   if N is even: value = low byte  |  (high byte's low  nibble << 8)
 *   if N is odd:  value = low byte's high nibble  |  (high byte << 4)
 */
static uint16_t fat12_get_entry(uint16_t cluster) {
    uint32_t offset = (cluster * 3) / 2;
    uint16_t val;
    if (cluster & 1)
        val = (fat_buf[offset] >> 4) | ((uint16_t)fat_buf[offset + 1] << 4);
    else
        val = fat_buf[offset] | (((uint16_t)fat_buf[offset + 1] & 0x0F) << 8);
    return val & 0x0FFF;
}

/* ── FAT12 entry write (in fat_buf only — call fat_flush to commit) ─────── */

static void fat12_set_entry(uint16_t cluster, uint16_t val) {
    uint32_t offset = (cluster * 3) / 2;
    val &= 0x0FFF;
    if (cluster & 1) {
        /* Odd: write val into high 12 bits of the two bytes */
        fat_buf[offset]     = (fat_buf[offset] & 0x0F) | ((val & 0x0F) << 4);
        fat_buf[offset + 1] = (uint8_t)(val >> 4);
    } else {
        /* Even: write val into low 12 bits */
        fat_buf[offset]     = (uint8_t)(val & 0xFF);
        fat_buf[offset + 1] = (fat_buf[offset + 1] & 0xF0) | ((val >> 8) & 0x0F);
    }
}

/* ── Flush fat_buf back to disk (both FAT copies) ───────────────────────── */

static int fat_flush(void) {
    uint8_t secs = g_fat_sectors;
    if (secs > 12) secs = 12;

    for (uint8_t copy = 0; copy < g_num_fats; copy++) {
        uint32_t lba = g_fat_lba + (uint32_t)copy * g_fat_sectors;
        /* Write one sector at a time (disk_write takes a count but let's be safe) */
        for (uint8_t s = 0; s < secs; s++) {
            if (disk_write_sectors(lba + s, 1, fat_buf + (uint32_t)s * 512) < 0)
                return FAT12_ERR_DISK;
        }
    }
    return FAT12_OK;
}

/* ── Find a free cluster (returns 0 if disk is full) ────────────────────── */

static uint16_t fat_find_free(void) {
    /* Valid data clusters start at 2.
       Total clusters = (data area size) / sectors_per_cluster.
       We cap the search at 4086 (max for FAT12). */
    for (uint16_t c = 2; c < 4086; c++) {
        if (fat12_get_entry(c) == FAT12_FREE)
            return c;
    }
    return 0;   /* 0 = none found (cluster 0 is reserved) */
}

/* ── Convert cluster number to first LBA of that cluster ────────────────── */

static uint32_t cluster_to_lba(uint16_t cluster) {
    return g_data_lba + ((uint32_t)(cluster - 2) * g_sectors_per_cluster);
}

/* ── Name parsing ───────────────────────────────────────────────────────── */

static void parse_83_name(const char* input, char out[11]) {
    k_memset(out, ' ', 11);
    int len = k_strlen(input);
    int dot_pos = -1;
    for (int j = 0; j < len; j++) {
        if (input[j] == '.') { dot_pos = j; break; }
    }
    int name_len = (dot_pos >= 0) ? dot_pos : len;
    if (name_len > 8) name_len = 8;
    for (int i = 0; i < name_len; i++)
        out[i] = (char)k_toupper((unsigned char)input[i]);
    if (dot_pos >= 0) {
        int ext_start = dot_pos + 1;
        int ext_len = len - ext_start;
        if (ext_len > 3) ext_len = 3;
        for (int i = 0; i < ext_len; i++)
            out[8 + i] = (char)k_toupper((unsigned char)input[ext_start + i]);
    }
}

/* ── Convert raw dir entry to FAT12_FileInfo ────────────────────────────── */

static void dir_entry_to_info(const FAT12_DirEntry* e, FAT12_FileInfo* info) {
    int i;
    for (i = 0; i < 8; i++) { if (e->name[i] == ' ') break; info->name[i] = e->name[i]; }
    info->name[i] = '\0';
    for (i = 0; i < 3; i++) { if (e->ext[i]  == ' ') break; info->ext[i]  = e->ext[i];  }
    info->ext[i] = '\0';
    info->attributes    = e->attributes;
    info->file_size     = e->file_size;
    info->first_cluster = e->cluster_low;
}

/* ── Public API — init ──────────────────────────────────────────────────── */

int fat12_init(uint32_t partition_lba) {
    g_partition_lba = partition_lba;

    if (disk_read_sectors(partition_lba, 1, sector_buf) < 0)
        return FAT12_ERR_DISK;

    FAT12_BPB* bpb = (FAT12_BPB*)sector_buf;

    if (sector_buf[510] != 0x55 || sector_buf[511] != 0xAA)
        return FAT12_ERR_CORRUPT;
    if (bpb->bytes_per_sector != 512)
        return FAT12_ERR_CORRUPT;

    g_bytes_per_sector    = bpb->bytes_per_sector;
    g_sectors_per_cluster = bpb->sectors_per_cluster;
    g_root_entry_count    = bpb->root_entry_count;
    g_fat_sectors         = (uint8_t)bpb->sectors_per_fat;
    g_num_fats            = bpb->num_fats;

    g_fat_lba      = partition_lba + bpb->reserved_sectors;
    g_root_dir_lba = g_fat_lba + ((uint32_t)bpb->num_fats * bpb->sectors_per_fat);

    uint32_t root_dir_bytes = (uint32_t)bpb->root_entry_count * 32;
    g_root_dir_sectors = (root_dir_bytes + 511) / 512;

    g_data_lba = g_root_dir_lba + g_root_dir_sectors;

    uint8_t fat_secs = g_fat_sectors;
    if (fat_secs > 12) fat_secs = 12;
    if (disk_read_sectors(g_fat_lba, fat_secs, fat_buf) < 0)
        return FAT12_ERR_DISK;

    g_initialized = 1;
    return FAT12_OK;
}

/* ── Public API — list ──────────────────────────────────────────────────── */

void fat12_list_root(void (*callback)(const FAT12_FileInfo* info)) {
    if (!g_initialized || !callback) return;

    FAT12_FileInfo info;
    for (uint32_t sec = 0; sec < g_root_dir_sectors; sec++) {
        if (disk_read_sectors(g_root_dir_lba + sec, 1, sector_buf) < 0)
            return;

        FAT12_DirEntry* entries = (FAT12_DirEntry*)sector_buf;
        for (int i = 0; i < 512 / 32; i++) {
            FAT12_DirEntry* e = &entries[i];
            if (e->name[0] == FAT_ENTRY_END) return;
            if ((uint8_t)e->name[0] == FAT_ENTRY_FREE) continue;
            if (e->attributes == FAT_ATTR_LFN)          continue;
            if (e->attributes & FAT_ATTR_VOLUME_ID)     continue;
            dir_entry_to_info(e, &info);
            callback(&info);
        }
    }
}

/* ── Public API — find ──────────────────────────────────────────────────── */

int fat12_find(const char* filename, FAT12_FileInfo* out) {
    if (!g_initialized) return FAT12_ERR_CORRUPT;

    char target[11];
    parse_83_name(filename, target);

    for (uint32_t sec = 0; sec < g_root_dir_sectors; sec++) {
        if (disk_read_sectors(g_root_dir_lba + sec, 1, sector_buf) < 0)
            return FAT12_ERR_DISK;

        FAT12_DirEntry* entries = (FAT12_DirEntry*)sector_buf;
        for (int i = 0; i < 512 / 32; i++) {
            FAT12_DirEntry* e = &entries[i];
            if (e->name[0] == FAT_ENTRY_END)           return FAT12_ERR_NOTFOUND;
            if ((uint8_t)e->name[0] == FAT_ENTRY_FREE) continue;
            if (e->attributes == FAT_ATTR_LFN)          continue;
            if (e->attributes & FAT_ATTR_VOLUME_ID)     continue;

            int match = 1;
            for (int j = 0; j < 8 && match; j++)
                if ((char)k_toupper((unsigned char)e->name[j]) != target[j]) match = 0;
            for (int j = 0; j < 3 && match; j++)
                if ((char)k_toupper((unsigned char)e->ext[j]) != target[8 + j]) match = 0;

            if (match) {
                if (out) dir_entry_to_info(e, out);
                return FAT12_OK;
            }
        }
    }
    return FAT12_ERR_NOTFOUND;
}

/* ── Public API — read ──────────────────────────────────────────────────── */

int fat12_read_file(const FAT12_FileInfo* info, uint8_t* buf, uint32_t buf_size) {
    if (!g_initialized || !info || !buf) return FAT12_ERR_CORRUPT;

    static uint8_t cluster_buf[8 * 512];
    uint32_t bytes_read = 0;
    uint32_t bytes_remaining = info->file_size;
    uint16_t cluster = info->first_cluster;

    while (cluster >= 2 && cluster < FAT12_EOC) {
        if (bytes_remaining == 0) break;

        uint32_t lba  = cluster_to_lba(cluster);
        uint8_t  secs = g_sectors_per_cluster;
        if (disk_read_sectors(lba, secs, cluster_buf) < 0)
            return FAT12_ERR_DISK;

        uint32_t cluster_bytes = (uint32_t)secs * 512;
        uint32_t to_copy = cluster_bytes;
        if (to_copy > bytes_remaining)          to_copy = bytes_remaining;
        if (bytes_read + to_copy > buf_size)    to_copy = buf_size - bytes_read;

        k_memcpy(buf + bytes_read, cluster_buf, to_copy);
        bytes_read      += to_copy;
        bytes_remaining -= to_copy;
        if (bytes_read >= buf_size) break;

        cluster = fat12_get_entry(cluster);
    }
    return (int)bytes_read;
}

int fat12_load(const char* filename, uint8_t* buf, uint32_t buf_size) {
    FAT12_FileInfo info;
    int result = fat12_find(filename, &info);
    if (result != FAT12_OK) return result;
    return fat12_read_file(&info, buf, buf_size);
}

/* ── Public API — write ─────────────────────────────────────────────────── */

/*
 * How fat12_write_file works:
 *
 * 1. If a file with this name already exists, free its cluster chain first
 *    (overwrite). Then mark its dir entry as deleted (0xE5).
 * 2. Allocate as many clusters as needed for the new data.
 *    Link them into a chain in fat_buf and mark the last with 0xFFF (EOC).
 * 3. Write each cluster's worth of data to the ramdisk via disk_write_sectors.
 * 4. Flush fat_buf to both FAT copies on disk.
 * 5. Write a new 32-byte directory entry into the first free slot in the
 *    root directory (0x00 = end-of-dir, 0xE5 = deleted — both are usable).
 *
 * The in-memory ramdisk is updated in all steps, so reads immediately after
 * this call will see the new file.
 */
int fat12_write_file(const char* filename, const uint8_t* buf, uint32_t size) {
    if (!g_initialized) return FAT12_ERR_CORRUPT;

    /* ── Step 1: delete existing file if present ───────────────────────── */
    fat12_delete_file(filename);   /* Ignore "not found" — that's fine */

    /* ── Step 2: allocate cluster chain ────────────────────────────────── */
    uint32_t cluster_bytes = (uint32_t)g_sectors_per_cluster * 512;
    uint32_t clusters_needed = (size + cluster_bytes - 1) / cluster_bytes;
    if (clusters_needed == 0) clusters_needed = 1;   /* Even empty files need 1 */

    uint16_t first_cluster = 0;
    uint16_t prev_cluster  = 0;

    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint16_t c = fat_find_free();
        if (c == 0) return FAT12_ERR_NOMEM;   /* Disk full */

        /* Temporarily mark as EOC so it looks allocated during search */
        fat12_set_entry(c, 0xFFF);

        if (i == 0)
            first_cluster = c;
        else
            fat12_set_entry(prev_cluster, c);   /* Link previous -> this */

        prev_cluster = c;
    }
    /* Last cluster is already 0xFFF (EOC) from the loop */

    /* ── Step 3: write data into clusters ───────────────────────────────── */
    static uint8_t cluster_buf[8 * 512];
    uint16_t cluster = first_cluster;
    uint32_t written = 0;

    while (cluster >= 2 && cluster < FAT12_EOC) {
        k_memset(cluster_buf, 0, cluster_bytes);   /* Zero-pad last cluster */

        uint32_t to_copy = size - written;
        if (to_copy > cluster_bytes) to_copy = cluster_bytes;
        k_memcpy(cluster_buf, buf + written, to_copy);

        uint32_t lba  = cluster_to_lba(cluster);
        uint8_t  secs = g_sectors_per_cluster;
        if (disk_write_sectors(lba, secs, cluster_buf) < 0)
            return FAT12_ERR_DISK;

        written += to_copy;
        cluster  = fat12_get_entry(cluster);
    }

    /* ── Step 4: flush FAT to disk (both copies) ────────────────────────── */
    if (fat_flush() != FAT12_OK) return FAT12_ERR_DISK;

    /* ── Step 5: write directory entry ─────────────────────────────────── */

    /* Build the padded 8.3 name */
    char name83[11];
    parse_83_name(filename, name83);

    /* Scan root directory for a free slot (0x00 or 0xE5) */
    for (uint32_t sec = 0; sec < g_root_dir_sectors; sec++) {
        if (disk_read_sectors(g_root_dir_lba + sec, 1, sector_buf) < 0)
            return FAT12_ERR_DISK;

        FAT12_DirEntry* entries = (FAT12_DirEntry*)sector_buf;

        for (int i = 0; i < 512 / 32; i++) {
            uint8_t first_byte = (uint8_t)entries[i].name[0];
            if (first_byte != FAT_ENTRY_END && first_byte != FAT_ENTRY_FREE)
                continue;   /* Slot in use */

            /* Fill in the directory entry */
            FAT12_DirEntry* e = &entries[i];
            k_memset(e, 0, 32);

            /* Name and extension (space-padded, already uppercase from parse) */
            for (int j = 0; j < 8; j++) e->name[j] = (uint8_t)name83[j];
            for (int j = 0; j < 3; j++) e->ext[j]  = (uint8_t)name83[8 + j];

            e->attributes  = FAT_ATTR_ARCHIVE;
            e->cluster_low = first_cluster;
            e->file_size   = size;

            /* Write the modified sector back */
            if (disk_write_sectors(g_root_dir_lba + sec, 1, sector_buf) < 0)
                return FAT12_ERR_DISK;

            return FAT12_OK;
        }
    }

    /* No free directory slot */
    return FAT12_ERR_NOMEM;
}

/* ── Public API — delete ────────────────────────────────────────────────── */

/*
 * How fat12_delete_file works:
 *
 * 1. Walk the root directory to find the entry by name.
 * 2. Follow its cluster chain in fat_buf, marking every cluster FAT12_FREE.
 * 3. Mark the directory entry's first byte as 0xE5 (deleted).
 * 4. Flush fat_buf so the freed clusters are visible immediately.
 */
int fat12_delete_file(const char* filename) {
    if (!g_initialized) return FAT12_ERR_CORRUPT;

    char target[11];
    parse_83_name(filename, target);

    for (uint32_t sec = 0; sec < g_root_dir_sectors; sec++) {
        if (disk_read_sectors(g_root_dir_lba + sec, 1, sector_buf) < 0)
            return FAT12_ERR_DISK;

        FAT12_DirEntry* entries = (FAT12_DirEntry*)sector_buf;

        for (int i = 0; i < 512 / 32; i++) {
            FAT12_DirEntry* e = &entries[i];

            if (e->name[0] == FAT_ENTRY_END)           return FAT12_ERR_NOTFOUND;
            if ((uint8_t)e->name[0] == FAT_ENTRY_FREE) continue;
            if (e->attributes == FAT_ATTR_LFN)          continue;
            if (e->attributes & FAT_ATTR_VOLUME_ID)     continue;

            int match = 1;
            for (int j = 0; j < 8 && match; j++)
                if ((char)k_toupper((unsigned char)e->name[j]) != target[j]) match = 0;
            for (int j = 0; j < 3 && match; j++)
                if ((char)k_toupper((unsigned char)e->ext[j]) != target[8 + j]) match = 0;

            if (!match) continue;

            /* Found it — free the cluster chain */
            uint16_t cluster = e->cluster_low;
            while (cluster >= 2 && cluster < FAT12_EOC) {
                uint16_t next = fat12_get_entry(cluster);
                fat12_set_entry(cluster, FAT12_FREE);
                cluster = next;
            }

            /* Mark directory entry as deleted */
            e->name[0] = FAT_ENTRY_FREE;
            if (disk_write_sectors(g_root_dir_lba + sec, 1, sector_buf) < 0)
                return FAT12_ERR_DISK;

            /* Flush FAT changes */
            return fat_flush();
        }
    }

    return FAT12_ERR_NOTFOUND;
}