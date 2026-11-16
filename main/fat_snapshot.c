#include "fat_snapshot.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | ((uint32_t)le16(p+2) << 16); }
typedef struct {
    fat_read_fn read; void *arg; uint64_t size, base;
    uint32_t fat, root, data, root_entries, root_cluster, clusters, sectors, fat_sectors;
    unsigned spc, type;
} volume_t;
static bool bytes(volume_t *v, uint64_t offset, void *out, size_t n)
{
    return offset <= v->size && n <= v->size - offset && v->read(v->arg, offset, out, n);
}
static bool sector(volume_t *v, uint32_t lba, uint8_t out[512])
{
    return bytes(v, v->base + (uint64_t)lba * 512, out, 512);
}
static bool cluster_valid(volume_t *v, uint32_t c) { return c >= 2 && c < v->clusters + 2; }
static bool next_cluster(volume_t *v, uint32_t c, uint32_t *next)
{
    uint64_t at = v->base + (uint64_t)v->fat * 512;
    uint8_t value[4];
    if (!cluster_valid(v, c)) return false;
    uint32_t offset = v->type == 12 ? c + c/2 : c * (v->type == 16 ? 2 : 4);
    size_t n = v->type == 32 ? 4 : 2;
    if ((uint64_t)offset + n > (uint64_t)v->fat_sectors * 512 || !bytes(v, at + offset, value, n)) return false;
    *next = v->type == 12 ? (le16(value) >> ((c & 1) ? 4 : 0)) & 0xfff :
        v->type == 16 ? le16(value) : le32(value) & 0x0fffffff;
    return true;
}
static bool end_of_chain(volume_t *v, uint32_t c)
{
    return c >= (v->type == 12 ? 0xff8u : v->type == 16 ? 0xfff8u : 0x0ffffff8u);
}
bool fat_snapshot_file(fat_read_fn read, void *arg, uint64_t size,
    const char name[11], char *out, size_t cap, size_t *length)
{
    volume_t v = {.read = read, .arg = arg, .size = size};
    uint8_t b[512];
    *length = 0;
    if (!cap || !sector(&v, 0, b)) return false;
    if (le16(b+11) != 512) {
        if (b[510] != 0x55 || b[511] != 0xaa) return false;
        v.base = (uint64_t)le32(b+454) * 512;
        if (!v.base || !sector(&v, 0, b)) return false;
    }
    if (b[510] != 0x55 || b[511] != 0xaa || le16(b+11) != 512) return false;
    v.spc = b[13];
    unsigned reserved = le16(b+14), fats = b[16];
    v.root_entries = le16(b+17);
    v.sectors = le16(b+19) ? le16(b+19) : le32(b+32);
    v.fat_sectors = le16(b+22) ? le16(b+22) : le32(b+36);
    if (!v.spc || (v.spc & (v.spc - 1)) || v.spc > 128 || !reserved || !fats || fats > 2 || !v.fat_sectors) return false;
    v.fat = reserved;
    uint64_t root = (uint64_t)reserved + (uint64_t)fats * v.fat_sectors;
    uint64_t data = root + ((uint64_t)v.root_entries * 32 + 511) / 512;
    if (data >= v.sectors || (uint64_t)v.sectors * 512 > v.size - v.base) return false;
    v.root = root; v.data = data;
    v.clusters = (v.sectors - v.data) / v.spc;
    v.type = v.clusters < 4085 ? 12 : v.clusters < 65525 ? 16 : 32;
    v.root_cluster = v.type == 32 ? le32(b+44) : 0;
    uint32_t file_cluster = 0, file_length = 0, c = v.root_cluster;
    bool found = false;
    // Directory traversal is bounded even for malformed cyclic FAT chains.
    for (unsigned visited = 0; visited < 128 && !found; visited++) {
        uint32_t start = v.root, count = (v.root_entries * 32 + 511) / 512;
        if (v.type == 32) {
            if (!cluster_valid(&v, c)) return false;
            start = v.data + (c - 2) * v.spc; count = v.spc;
        }
        for (uint32_t i = 0; i < count && !found; i++) {
            if (!sector(&v, start+i, b)) return false;
            for (unsigned j = 0; j < 512; j += 32) {
                if (b[j] == 0) return false;
                if (b[j] == 0xe5 || (b[j+11] & 0x18) || b[j+11] == 0x0f) continue;
                if (!memcmp(b+j, name, 11)) {
                    file_cluster = le16(b+j+26) | (v.type == 32 ? (uint32_t)le16(b+j+20) << 16 : 0);
                    file_length = le32(b+j+28); found = true; break;
                }
            }
        }
        if (found) break;
        if (v.type != 32 || !next_cluster(&v, c, &c) || end_of_chain(&v, c)) return false;
    }
    if (!found || file_length >= cap) return false;
    if (!file_length) { out[0] = 0; return true; }
    c = file_cluster;
    size_t copied = 0;
    unsigned limit = file_length / (v.spc * 512) + 1;
    uint32_t seen[32]; unsigned seen_count = 0;
    for (unsigned visits = 0; copied < file_length && visits < limit; visits++) {
        if (!cluster_valid(&v, c) || seen_count == 32) return false;
        for (unsigned i = 0; i < seen_count; i++) if (seen[i] == c) return false;
        seen[seen_count++] = c;
        size_t n = file_length - copied;
        if (n > v.spc * 512) n = v.spc * 512;
        if (!bytes(&v, v.base + ((uint64_t)v.data + (uint64_t)(c-2) * v.spc) * 512, out+copied, n)) return false;
        copied += n;
        if (copied < file_length && (!next_cluster(&v, c, &c) || end_of_chain(&v, c))) return false;
    }
    if (copied != file_length) return false;
    out[copied] = 0; *length = copied;
    return true;
}
