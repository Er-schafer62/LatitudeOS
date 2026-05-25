#!/usr/bin/env python3
"""
mkimage.py — Assemble or patch myos.img from compiled binaries + fat12.img.

Disk layout (512-byte sectors):
  Sector  0      : boot.bin        (1 sector)
  Sectors 1-4    : stage2.bin      (4 sectors)
  Sectors 5-36   : kernel.bin      (32 sectors = 16KB max)
  Sectors 37     : padding
  Sectors 38+    : FAT12 volume    (from build/fat12.img — 2880 sectors)

FAT12 starts at sector 38 = LBA 38
(must match RAMDISK_LBA in disk.c and stage2.asm)

Usage:
  python mkimage.py              -> assemble full image (needs fat12.img)
  python mkimage.py --patch-only -> re-embed kernel only, leave FAT12 untouched

IMPORTANT: Run makedisk.py first to create fat12.img if it doesn't exist.
           The FAT12 image is kept separately so rebuilds don't wipe your files.
"""

import os, sys, struct

BUILD  = "build"
SECTOR = 512

KERNEL_START_LBA   = 5
KERNEL_SECTORS     = 48     # 16 KB kernel max
FAT12_START_LBA    = 54     # 1 + 4 + 32 + 1 padding

FAT12_IMG          = os.path.join(BUILD, "fat12.img")

def pad_to(data, sectors):
    target = sectors * SECTOR
    if len(data) > target:
        print(f"  WARNING: binary is {len(data)} bytes, truncating to {target}")
        return data[:target]
    return data + b'\x00' * (target - len(data))

def read_bin(path, max_sectors):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) > max_sectors * SECTOR:
        print(f"  WARNING: {path} truncated to {max_sectors * SECTOR} bytes")
        data = data[:max_sectors * SECTOR]
    return data

def read_fat12():
    if not os.path.exists(FAT12_IMG):
        print(f"  ERROR: {FAT12_IMG} not found!")
        print(f"  Run:  python tools\\makedisk.py")
        print(f"  Then: python tools\\addfiles.py   (optional default files)")
        sys.exit(1)
    with open(FAT12_IMG, "rb") as f:
        data = f.read()
    expected = 2880 * SECTOR
    if len(data) < expected:
        print(f"  WARNING: fat12.img is {len(data)} bytes, expected {expected}. Padding.")
        data = data + b'\x00' * (expected - len(data))
    return data[:expected]

def fresh_build():
    img_path = os.path.join(BUILD, "myos.img")
    print("  Reading binaries...")
    boot   = pad_to(read_bin(os.path.join(BUILD, "boot.bin"),   1),              1)
    stage2 = pad_to(read_bin(os.path.join(BUILD, "stage2.bin"), 4),              4)
    kernel = pad_to(read_bin(os.path.join(BUILD, "kernel.bin"), KERNEL_SECTORS), KERNEL_SECTORS)

    print(f"  Reading persistent FAT12 volume from {FAT12_IMG}...")
    fat12 = read_fat12()

    total_sectors = FAT12_START_LBA + len(fat12) // SECTOR
    print("  Assembling disk image...")
    image = bytearray(total_sectors * SECTOR)
    image[0 * SECTOR : 1 * SECTOR]                         = boot
    image[1 * SECTOR : 5 * SECTOR]                         = stage2
    image[5 * SECTOR : (5 + KERNEL_SECTORS) * SECTOR]      = kernel
    image[FAT12_START_LBA * SECTOR:]                        = fat12

    with open(img_path, "wb") as f:
        f.write(image)

    kernel_actual = os.path.getsize(os.path.join(BUILD, "kernel.bin"))
    size_kb = len(image) // 1024
    print(f"  Done! {img_path} ({size_kb} KB, {len(image)} bytes)")
    print(f"  Kernel: {kernel_actual} bytes / {KERNEL_SECTORS * SECTOR} bytes max "
          f"({100 * kernel_actual // (KERNEL_SECTORS * SECTOR)}% used)")
    if kernel_actual > KERNEL_SECTORS * SECTOR:
        print(f"  WARNING: Kernel exceeds {KERNEL_SECTORS} sectors! Increase KERNEL_SECTORS.")

def patch_kernel():
    img_path = os.path.join(BUILD, "myos.img")
    if not os.path.exists(img_path):
        print("  ERROR: No existing image. Run without --patch-only first.")
        sys.exit(1)
    print("  Reading existing image...")
    with open(img_path, "rb") as f:
        image = bytearray(f.read())
    print("  Patching boot + stage2 + kernel (FAT12 untouched)...")
    boot   = pad_to(read_bin(os.path.join(BUILD, "boot.bin"),   1),              1)
    stage2 = pad_to(read_bin(os.path.join(BUILD, "stage2.bin"), 4),              4)
    kernel = pad_to(read_bin(os.path.join(BUILD, "kernel.bin"), KERNEL_SECTORS), KERNEL_SECTORS)
    image[0 * SECTOR : 1 * SECTOR]                         = boot
    image[1 * SECTOR : 5 * SECTOR]                         = stage2
    image[5 * SECTOR : (5 + KERNEL_SECTORS) * SECTOR]      = kernel
    with open(img_path, "wb") as f:
        f.write(image)
    kernel_actual = os.path.getsize(os.path.join(BUILD, "kernel.bin"))
    print(f"  Kernel patched. {kernel_actual} bytes / {KERNEL_SECTORS * SECTOR} bytes max. FAT12 preserved.")

if __name__ == "__main__":
    os.makedirs(BUILD, exist_ok=True)
    if "--patch-only" in sys.argv:
        patch_kernel()
    else:
        fresh_build()