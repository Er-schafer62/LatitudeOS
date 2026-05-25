#ifndef DISK_H
#define DISK_H

#include <stdint.h>

/* Read 'count' 512-byte sectors from LBA 'lba' into 'buf'.
   Returns 0 on success, -1 on error. */
int disk_read_sectors(uint32_t lba, uint8_t count, void* buf);

/* Write 'count' 512-byte sectors from 'buf' to LBA 'lba' in the ramdisk.
   Returns 0 on success, -1 on error. */
int disk_write_sectors(uint32_t lba, uint8_t count, const void* buf);

#endif