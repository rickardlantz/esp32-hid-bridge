#include "fat_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_image(void *arg, uint64_t offset, void *buffer, size_t n)
{
    FILE *f = arg;
    return !fseek(f, offset, SEEK_SET) && fread(buffer, 1, n, f) == n;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *image = fopen(argv[1], "rb"), *expected = fopen(argv[2], "rb");
    assert(image && expected);
    fseek(image, 0, SEEK_END); uint64_t size = ftell(image);
    char result[8193], want[8193]; size_t length;
    size_t wanted = fread(want, 1, sizeof(want), expected);
    assert(fat_snapshot_file(read_image, image, size, "CONFIG  INI", result, sizeof(result), &length));
    assert(length == wanted && !memcmp(result, want, wanted) && result[length] == 0);
    assert(!fat_snapshot_file(read_image, image, size, "MISSING INI", result, sizeof(result), &length));
    assert(!fat_snapshot_file(read_image, image, size, "CONFIG  INI", result, 2, &length));
    fclose(image); fclose(expected);
    puts("FAT snapshot image tests passed");
}
