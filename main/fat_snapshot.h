#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*fat_read_fn)(void *arg, uint64_t offset, void *buffer, size_t length);
// Read-only root-directory file snapshot. Supports FAT12/16/32, superfloppy
// and an MBR first partition. The caller serializes writes for the whole call.
bool fat_snapshot_file(fat_read_fn read, void *arg, uint64_t disk_size,
    const char short_name[11], char *out, size_t capacity, size_t *length);
