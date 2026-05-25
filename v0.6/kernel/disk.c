/* disk.c - RAM-backed sector reader/writer
 *
 * Stage2 loaded the FAT12 volume into RAM at 0x20000 using BIOS int 0x13.
 * RAMDISK_LBA must match FAT12_START_LBA in mkimage.py (38)
 * and RAMDISK_LBA in stage2.asm (38).
 *
 * Writes go directly to the ramdisk copy in RAM — changes are visible
 * immediately but are lost when the VM is powered off (no ATA write driver).
 */

#include "disk.h"
#include <stdint.h>

#define RAMDISK_BASE    0x20000
#define RAMDISK_LBA     54
#define RAMDISK_SECTORS 64

int disk_read_sectors(uint32_t lba, uint8_t count, void* buf) {
    if (lba < RAMDISK_LBA) return -1;

    uint32_t offset = lba - RAMDISK_LBA;
    if (offset + count > RAMDISK_SECTORS) return -1;

    const uint8_t* src = (const uint8_t*)(RAMDISK_BASE + offset * 512);
    uint8_t* dst = (uint8_t*)buf;

    uint32_t bytes = (uint32_t)count * 512;
    for (uint32_t i = 0; i < bytes; i++)
        dst[i] = src[i];

    return 0;
}

int disk_write_sectors(uint32_t lba, uint8_t count, const void* buf) {
    if (lba < RAMDISK_LBA) return -1;

    uint32_t offset = lba - RAMDISK_LBA;
    if (offset + count > RAMDISK_SECTORS) return -1;

    uint8_t* dst = (uint8_t*)(RAMDISK_BASE + offset * 512);
    const uint8_t* src = (const uint8_t*)buf;

    uint32_t bytes = (uint32_t)count * 512;
    for (uint32_t i = 0; i < bytes; i++)
        dst[i] = src[i];

    return 0;
}