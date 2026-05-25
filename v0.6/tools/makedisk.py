#!/usr/bin/env python3
"""
makedisk.py — Create or reset the persistent FAT12 disk image.

Usage:
  python makedisk.py           -> create build/fat12.img if it doesn't exist
  python makedisk.py --reset   -> wipe and recreate build/fat12.img (clears all files)
  python makedisk.py --status  -> show disk usage info

The disk image is a standard 1.44MB FAT12 volume (2880 sectors).
It lives at build/fat12.img and is kept across rebuilds.
Only run --reset when you want to wipe user files.

After creating the disk, call addfiles.py to populate it with initial files.
"""

import os, sys, struct

BUILD   = "build"
IMG     = os.path.join(BUILD, "fat12.img")
SECTOR  = 512

# ── FAT12 geometry (standard 1.44MB floppy) ─────────────────────────────────
TOTAL_SECTORS       = 2880
SECTORS_PER_TRACK   = 18
NUM_HEADS           = 2
RESERVED_SECTORS    = 1
NUM_FATS            = 2
ROOT_ENTRY_COUNT    = 224
SECTORS_PER_FAT     = 9
SECTORS_PER_CLUSTER = 1
MEDIA_TYPE          = 0xF0

ROOT_DIR_SECTORS = (ROOT_ENTRY_COUNT * 32 + SECTOR - 1) // SECTOR   # 14
DATA_START_SECTOR = RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT + ROOT_DIR_SECTORS  # 33

VOLUME_LABEL = b"MYOS       "
OEM_NAME     = b"MYOS0.1 "

# ── BPB builder ─────────────────────────────────────────────────────────────

def make_bpb():
    bpb = bytearray(SECTOR)
    bpb[0] = 0xEB; bpb[1] = 0x3C; bpb[2] = 0x90
    bpb[3:11]  = OEM_NAME
    struct.pack_into('<H', bpb, 11, SECTOR)
    bpb[13]    = SECTORS_PER_CLUSTER
    struct.pack_into('<H', bpb, 14, RESERVED_SECTORS)
    bpb[16]    = NUM_FATS
    struct.pack_into('<H', bpb, 17, ROOT_ENTRY_COUNT)
    struct.pack_into('<H', bpb, 19, TOTAL_SECTORS)
    bpb[21]    = MEDIA_TYPE
    struct.pack_into('<H', bpb, 22, SECTORS_PER_FAT)
    struct.pack_into('<H', bpb, 24, SECTORS_PER_TRACK)
    struct.pack_into('<H', bpb, 26, NUM_HEADS)
    struct.pack_into('<I', bpb, 28, 0)   # hidden sectors
    struct.pack_into('<I', bpb, 32, 0)   # total sectors 32
    bpb[36]    = 0x80                    # drive number
    bpb[37]    = 0x00
    bpb[38]    = 0x29                    # extended boot signature
    struct.pack_into('<I', bpb, 39, 0xDEADBEEF)   # volume serial
    bpb[43:54] = VOLUME_LABEL
    bpb[54:62] = b"FAT12   "
    bpb[510]   = 0x55
    bpb[511]   = 0xAA
    return bytes(bpb)

def make_fat():
    fat = bytearray(SECTORS_PER_FAT * SECTOR)
    fat[0] = 0xF0; fat[1] = 0xFF; fat[2] = 0xFF   # media descriptor + 2 reserved entries
    return bytes(fat)

def make_fat12_volume():
    volume = bytearray(TOTAL_SECTORS * SECTOR)
    volume[0:SECTOR] = make_bpb()
    fat = make_fat()
    fat1_start = RESERVED_SECTORS * SECTOR
    volume[fat1_start : fat1_start + len(fat)] = fat
    fat2_start = fat1_start + SECTORS_PER_FAT * SECTOR
    volume[fat2_start : fat2_start + len(fat)] = fat
    return bytes(volume)

# ── FAT12 reader for --status ────────────────────────────────────────────────

def fat12_get(fat, n):
    offset = (n * 3) // 2
    if n & 1:
        return ((fat[offset] >> 4) | (fat[offset + 1] << 4)) & 0xFFF
    else:
        return (fat[offset] | ((fat[offset + 1] & 0x0F) << 8)) & 0xFFF

def count_free_clusters(data):
    """Count free clusters in the FAT loaded from fat12.img."""
    fat_start = RESERVED_SECTORS * SECTOR
    fat = data[fat_start : fat_start + SECTORS_PER_FAT * SECTOR]
    total_clusters = TOTAL_SECTORS - DATA_START_SECTOR
    free = 0
    for c in range(2, total_clusters + 2):
        if fat12_get(fat, c) == 0x000:
            free += 1
    return free, total_clusters

def list_root_files(data):
    root_start = (RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT) * SECTOR
    files = []
    for i in range(ROOT_ENTRY_COUNT):
        e_off = root_start + i * 32
        first = data[e_off]
        if first == 0x00:
            break
        if first == 0xE5:
            continue
        attr = data[e_off + 11]
        if attr == 0x0F or (attr & 0x08):
            continue
        raw_name = data[e_off:e_off+8].rstrip(b' ').decode('ascii', errors='replace')
        raw_ext  = data[e_off+8:e_off+11].rstrip(b' ').decode('ascii', errors='replace')
        size = struct.unpack_from('<I', data, e_off + 28)[0]
        name = raw_name + ('.' + raw_ext if raw_ext else '')
        files.append((name, size))
    return files

# ── Commands ─────────────────────────────────────────────────────────────────

def cmd_create():
    os.makedirs(BUILD, exist_ok=True)
    if os.path.exists(IMG):
        print(f"  {IMG} already exists — skipping creation.")
        print(f"  Use 'makedisk.py --reset' to wipe it.")
        return
    print(f"  Creating fresh FAT12 disk: {IMG}")
    volume = make_fat12_volume()
    with open(IMG, 'wb') as f:
        f.write(volume)
    size_kb = len(volume) // 1024
    print(f"  Done! {IMG} ({size_kb} KB, {TOTAL_SECTORS} sectors)")
    print(f"  Run 'addfiles.py' to populate it with initial files.")

def cmd_reset():
    os.makedirs(BUILD, exist_ok=True)
    if os.path.exists(IMG):
        print(f"  Wiping existing {IMG}...")
    else:
        print(f"  Creating new {IMG}...")
    volume = make_fat12_volume()
    with open(IMG, 'wb') as f:
        f.write(volume)
    print(f"  Done! FAT12 disk reset. All files cleared.")
    print(f"  Run 'addfiles.py' to repopulate with default files.")

def cmd_status():
    if not os.path.exists(IMG):
        print(f"  {IMG} does not exist. Run 'makedisk.py' to create it.")
        return
    with open(IMG, 'rb') as f:
        data = f.read()
    free_clusters, total_clusters = count_free_clusters(data)
    used_clusters = total_clusters - free_clusters
    used_kb  = used_clusters * SECTORS_PER_CLUSTER * SECTOR // 1024
    free_kb  = free_clusters * SECTORS_PER_CLUSTER * SECTOR // 1024
    total_kb = total_clusters * SECTORS_PER_CLUSTER * SECTOR // 1024
    print(f"  Disk: {IMG}")
    print(f"  Size: {total_kb} KB total, {used_kb} KB used, {free_kb} KB free")
    print(f"  Clusters: {used_clusters}/{total_clusters} used")
    files = list_root_files(data)
    if files:
        print(f"  Files ({len(files)}):")
        for name, size in files:
            print(f"    {name:<16} {size:>6} bytes")
    else:
        print("  No files on disk.")

# ── Main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if "--reset" in sys.argv:
        cmd_reset()
    elif "--status" in sys.argv:
        cmd_status()
    else:
        cmd_create()