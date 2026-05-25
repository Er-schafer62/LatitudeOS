#!/usr/bin/env python3
"""
addfiles.py — Add default files to build/fat12.img.

This script writes directly to fat12.img, not to myos.img.
Run it after makedisk.py to populate the disk with initial files.
It is safe to run multiple times — it skips files that already exist
(by name) unless you pass --force to overwrite them.

Usage:
  python addfiles.py          -> add missing files only
  python addfiles.py --force  -> overwrite all files in the FILES list
"""

import os, sys, struct

BUILD        = "build"
IMG_PATH     = os.path.join(BUILD, "fat12.img")
SECTOR       = 512

# ── FAT12 geometry (must match makedisk.py) ──────────────────────────────────
TOTAL_SECTORS       = 2880
RESERVED_SECTORS    = 1
NUM_FATS            = 2
SECTORS_PER_FAT     = 9
ROOT_ENTRY_COUNT    = 224
ROOT_DIR_SECTORS    = (ROOT_ENTRY_COUNT * 32 + SECTOR - 1) // SECTOR   # 14
SECTORS_PER_CLUSTER = 1
DATA_START_SECTOR   = RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT + ROOT_DIR_SECTORS  # 33
TOTAL_CLUSTERS      = TOTAL_SECTORS - DATA_START_SECTOR

# ── FAT12 helpers ─────────────────────────────────────────────────────────────

def fat12_get(fat, n):
    offset = (n * 3) // 2
    if n & 1:
        return ((fat[offset] >> 4) | (fat[offset + 1] << 4)) & 0xFFF
    else:
        return (fat[offset] | ((fat[offset + 1] & 0x0F) << 8)) & 0xFFF

def fat12_set(fat, n, val):
    offset = (n * 3) // 2
    if n & 1:
        fat[offset]     = (fat[offset] & 0x0F) | ((val & 0x0F) << 4)
        fat[offset + 1] = (val >> 4) & 0xFF
    else:
        fat[offset]     = val & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((val >> 8) & 0x0F)

def find_free_cluster(fat, start=2):
    for n in range(start, TOTAL_CLUSTERS + 2):
        if fat12_get(fat, n) == 0x000:
            return n
    return None

def to_83(filename):
    filename = filename.upper()
    dot = filename.rfind('.')
    if dot >= 0:
        name = filename[:dot]; ext = filename[dot+1:]
    else:
        name = filename; ext = ''
    if len(name) > 8 or len(ext) > 3:
        raise ValueError(f"'{filename}' does not fit 8.3 format")
    return name.encode('ascii').ljust(8, b' '), ext.encode('ascii').ljust(3, b' ')

def make_dir_entry(name83, ext83, first_cluster, file_size):
    entry = bytearray(32)
    entry[0:8]  = name83
    entry[8:11] = ext83
    entry[11]   = 0x20   # Archive attribute
    struct.pack_into('<H', entry, 26, first_cluster)
    struct.pack_into('<I', entry, 28, file_size)
    return bytes(entry)

# ── FAT12Image class ──────────────────────────────────────────────────────────

class FAT12Image:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.raw = bytearray(f.read())
        self.path = path

    def _off(self, sector):
        return sector * SECTOR

    def read_sector(self, sector):
        o = self._off(sector)
        return bytearray(self.raw[o : o + SECTOR])

    def write_sector(self, sector, data):
        o = self._off(sector)
        self.raw[o : o + SECTOR] = data

    def read_fat(self):
        fat = bytearray()
        for s in range(SECTORS_PER_FAT):
            fat += self.read_sector(RESERVED_SECTORS + s)
        return fat

    def write_fat(self, fat):
        for copy in range(NUM_FATS):
            start = RESERVED_SECTORS + copy * SECTORS_PER_FAT
            for s in range(SECTORS_PER_FAT):
                self.write_sector(start + s, fat[s*SECTOR:(s+1)*SECTOR])

    def cluster_to_sector(self, cluster):
        return DATA_START_SECTOR + (cluster - 2) * SECTORS_PER_CLUSTER

    def write_cluster(self, cluster, data):
        sec = self.cluster_to_sector(cluster)
        if len(data) < SECTOR:
            data = data + b'\x00' * (SECTOR - len(data))
        self.write_sector(sec, data[:SECTOR])

    def file_exists(self, filename):
        """Return True if filename (case-insensitive) is already in root dir."""
        try:
            name83, ext83 = to_83(filename)
        except ValueError:
            return False
        root_dir_start = RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT
        for s in range(ROOT_DIR_SECTORS):
            sec_data = self.read_sector(root_dir_start + s)
            for i in range(SECTOR // 32):
                e_off = i * 32
                first = sec_data[e_off]
                if first == 0x00:
                    return False
                if first == 0xE5:
                    continue
                if sec_data[e_off:e_off+8] == name83 and sec_data[e_off+8:e_off+11] == ext83:
                    return True
        return False

    def add_file(self, filename, data):
        name83, ext83 = to_83(filename)
        fat = self.read_fat()
        file_size    = len(data)
        num_clusters = max(1, (file_size + SECTOR - 1) // SECTOR)
        clusters     = []
        search_from  = 2

        for _ in range(num_clusters):
            c = find_free_cluster(fat, search_from)
            if c is None:
                print(f"  ERROR: Disk full, cannot add {filename}")
                return False
            clusters.append(c)
            fat12_set(fat, c, 0xFFF)
            search_from = c + 1

        for i in range(len(clusters) - 1):
            fat12_set(fat, clusters[i], clusters[i + 1])
        fat12_set(fat, clusters[-1], 0xFFF)

        for i, cluster in enumerate(clusters):
            self.write_cluster(cluster, data[i*SECTOR:(i+1)*SECTOR])

        self.write_fat(fat)

        root_dir_start = RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT
        entry = make_dir_entry(name83, ext83, clusters[0], file_size)
        for s in range(ROOT_DIR_SECTORS):
            sec_data = bytearray(self.read_sector(root_dir_start + s))
            for i in range(SECTOR // 32):
                e_off = i * 32
                if sec_data[e_off] == 0x00 or sec_data[e_off] == 0xE5:
                    sec_data[e_off : e_off + 32] = entry
                    self.write_sector(root_dir_start + s, sec_data)
                    return True
        print(f"  ERROR: Root directory full")
        return False

    def save(self):
        with open(self.path, "wb") as f:
            f.write(self.raw)

# ── Default files to add ──────────────────────────────────────────────────────
# Edit this list to control what gets placed on the disk on first creation.
# Keys: filename (8.3), value: bytes content.

FILES = [
    ("HELLO.TXT",  b"Hello from MyOS FAT12!\r\nWelcome to your filesystem.\r\n"),
    ("README.TXT", b"MyOS v0.5 with FAT12 read/write support.\r\n"
                   b"Commands: ls, cat, save, rm, get, netls, write, lwrite\r\n"),
    ("TEST.TXT",   b"This is a test file.\r\nLine 2.\r\nLine 3.\r\n"),
]

# ─────────────────────────────────────────────────────────────────────────────

def main():
    force = "--force" in sys.argv

    if not os.path.exists(IMG_PATH):
        print(f"ERROR: {IMG_PATH} not found.")
        print(f"Run:  python tools\\makedisk.py")
        return

    print(f"Opening {IMG_PATH}...")
    img = FAT12Image(IMG_PATH)
    added = 0
    skipped = 0

    for filename, data in FILES:
        if not force and img.file_exists(filename):
            print(f"  Skipping {filename} (already exists, use --force to overwrite)")
            skipped += 1
            continue
        print(f"  Adding {filename} ({len(data)} bytes)...")
        if img.add_file(filename, data):
            added += 1

    img.save()
    print(f"Done! {added} file(s) added, {skipped} skipped.")

if __name__ == "__main__":
    main()