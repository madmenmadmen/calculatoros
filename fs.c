#include "fs.h"
#include "block.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    block_device_t* dev;
    uint8_t block_bitmap[TOTAL_BLOCKS / 8];
    uint8_t inode_bitmap[BLOCK_SIZE];

    inode_t inode_cache[MAX_INODES];
    int inode_cache_loaded[MAX_INODES];
    int inode_cache_dirty[MAX_INODES];

    int current_dir;
    char mount_point[MAX_PATH];
    int mounted;
} fs_instance_t;

extern void print(const char*);
extern void print_char(char);
static void load_inode(fs_instance_t* fs, int i);
static void save_inode(fs_instance_t* fs, int i);
static int alloc_block(fs_instance_t* fs);
static void free_block(fs_instance_t* fs, int block);
static int alloc_inode(fs_instance_t* fs);
static void free_inode(fs_instance_t* fs, int i);
static void load_bitmap(fs_instance_t* fs);
static void save_bitmap(fs_instance_t* fs);
static void load_inode_bitmap(fs_instance_t* fs);
static void save_inode_bitmap(fs_instance_t* fs);
static void dev_read(block_device_t* dev, uint32_t lba, uint8_t count, void* buf);
static void dev_write(block_device_t* dev, uint32_t lba, uint8_t count, const void* buf);

static fs_instance_t instances[MAX_MOUNTS];
static int instance_count = 0;
static fs_instance_t* current_fs = 0;

typedef struct {
    char path[MAX_PATH];
    fs_instance_t* fs;
} vfs_mount_t;

static vfs_mount_t vfs_mounts[MAX_MOUNTS];
static int vfs_mount_count = 0;

static int valid_inode(int i) {
    return i >= 0 && i < MAX_INODES;
}

static void* memcpy(void* dest, const void* src, int n) {
    char* d = dest;
    const char* s = src;
    for (int i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static void dev_read(block_device_t* dev, uint32_t lba, uint8_t count, void* buf) {
    dev->read(dev, lba, count, buf);
}

static void dev_write(block_device_t* dev, uint32_t lba, uint8_t count, const void* buf) {
    dev->write(dev, lba, count, buf);
}

static int dir_add_child(fs_instance_t* fs, int parent, int child)
{
    load_inode(fs, parent);
    inode_t* dir = &fs->inode_cache[parent];

    uint32_t buf[128];

    if (dir->first_child_block == (uint32_t)-1) {
        int nb = alloc_block(fs);
        if (nb == -1) return 0;

        dir->first_child_block = nb;

        for (int i = 0; i < 128; i++)
            buf[i] = (uint32_t)-1;

        buf[0] = child;

        dev_write(fs->dev, DATA_START + nb, 1, buf);

        fs->inode_cache_dirty[parent] = 1;
        save_inode(fs, parent);
        return 1;
    }

    dev_read(fs->dev, DATA_START + dir->first_child_block, 1, buf);

    for (int i = 0; i < 128; i++) {
        if (buf[i] == (uint32_t)-1) {
            buf[i] = child;
            dev_write(fs->dev, DATA_START + dir->first_child_block, 1, buf);

            fs->inode_cache_dirty[parent] = 1;
            save_inode(fs, parent);

            return 1;
        }
    }

    print("DIR FULL\n");
    return 0;
}

static int get_children(fs_instance_t* fs, int parent, uint32_t* out)
{
    load_inode(fs, parent);
    inode_t* dir = &fs->inode_cache[parent];

    if (dir->type != TYPE_DIR) return 0;

    if (dir->first_child_block == (uint32_t)-1) return 0;
    if (dir->first_child_block >= TOTAL_BLOCKS) return 0;

    uint32_t buf[128];
    dev_read(fs->dev, DATA_START + dir->first_child_block, 1, buf);

    int count = 0;
    for (int i = 0; i < 128; i++) {
        if (buf[i] == (uint32_t)-1) continue;
        if (!valid_inode(buf[i])) continue;

        if (count >= 128) {
            print("get_children overflow\n");
            break;
        }

        out[count++] = buf[i];
    }

    return count;
}

static void dir_remove_child(fs_instance_t* fs, int parent, int child)
{
    load_inode(fs, parent);
    inode_t* dir = &fs->inode_cache[parent];

    if (dir->first_child_block == (uint32_t)-1) return;

    uint32_t buf[128];
    dev_read(fs->dev, DATA_START + dir->first_child_block, 1, buf);

    for (int i = 0; i < 128; i++) {
        if (buf[i] == child) {
            buf[i] = (uint32_t)-1;
            break;
        }
    }

    dev_write(fs->dev, DATA_START + dir->first_child_block, 1, buf);
}

static void load_inode(fs_instance_t* fs, int i) {
    if (!valid_inode(i)) return;
    if (fs->inode_cache_loaded[i]) return;

    uint8_t buf[BLOCK_SIZE];
    int sector = INODE_START + (i * INODE_SIZE) / BLOCK_SIZE;
    int offset = (i * INODE_SIZE) % BLOCK_SIZE;

    dev_read(fs->dev, sector, 1, buf);
    memcpy(&fs->inode_cache[i], buf + offset, INODE_SIZE);
    fs->inode_cache_loaded[i] = 1;
    fs->inode_cache_dirty[i] = 0;
}

static void save_inode(fs_instance_t* fs, int i) {
    if (!valid_inode(i)) return;
    if (!fs->inode_cache_dirty[i]) return;

    uint8_t buf[BLOCK_SIZE];
    int sector = INODE_START + (i * INODE_SIZE) / BLOCK_SIZE;
    int offset = (i * INODE_SIZE) % BLOCK_SIZE;

    dev_read(fs->dev, sector, 1, buf);
    memcpy(buf + offset, &fs->inode_cache[i], INODE_SIZE);
    dev_write(fs->dev, sector, 1, buf);
    fs->inode_cache_dirty[i] = 0;
}

static void load_inode_bitmap(fs_instance_t* fs) {
    int sectors = (MAX_INODES / 8 + 511) / BLOCK_SIZE;
    for (int i = 0; i < sectors; i++) {
        dev_read(fs->dev, INODE_BITMAP_START + i, 1, fs->inode_bitmap + i * BLOCK_SIZE);
    }
}

static void save_inode_bitmap(fs_instance_t* fs) {
    int sectors = (MAX_INODES / 8 + 511) / BLOCK_SIZE;
    for (int i = 0; i < sectors; i++) {
        dev_write(fs->dev, INODE_BITMAP_START + i, 1, fs->inode_bitmap + i * BLOCK_SIZE);
    }
}

static void load_bitmap(fs_instance_t* fs) {
    int sectors = (TOTAL_BLOCKS + 7) / 8 / BLOCK_SIZE + 1;
    for (int i = 0; i < sectors; i++) {
        dev_read(fs->dev, BLOCK_BITMAP_START + i, 1, fs->block_bitmap + i * BLOCK_SIZE);
    }
}

static void save_bitmap(fs_instance_t* fs) {
    int sectors = (TOTAL_BLOCKS + 7) / 8 / BLOCK_SIZE + 1;
    for (int i = 0; i < sectors; i++) {
        dev_write(fs->dev, BLOCK_BITMAP_START + i, 1, fs->block_bitmap + i * BLOCK_SIZE);
    }
}

static int alloc_inode(fs_instance_t* fs) {
    for (int i = 1; i < MAX_INODES; i++) {
        int b = i / 8;
        int bit = i % 8;
        if (!(fs->inode_bitmap[b] & (1 << bit))) {
            fs->inode_bitmap[b] |= (1 << bit);
            save_inode_bitmap(fs);
            return i;
        }
    }
    return -1;
}

static void free_inode(fs_instance_t* fs, int i) {
    int b = i / 8;
    int bit = i % 8;
    fs->inode_bitmap[b] &= ~(1 << bit);
    save_inode_bitmap(fs);
}

static int alloc_block(fs_instance_t* fs) {
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        int b = i / 8;
        int bit = i % 8;
        if (!(fs->block_bitmap[b] & (1 << bit))) {
            fs->block_bitmap[b] |= (1 << bit);
            save_bitmap(fs);
            return i;
        }
    }
    return -1;
}

static void free_block(fs_instance_t* fs, int block) {
    if (block < 0 || block >= TOTAL_BLOCKS) return;
    int b = block / 8;
    int bit = block % 8;
    fs->block_bitmap[b] &= ~(1 << bit);
    save_bitmap(fs);
}

static int find_inode(fs_instance_t* fs, const char* path) {
    if (!path || !path[0]) return fs->current_dir;

    int cur = (path[0] == '/') ? 0 : fs->current_dir;
    char tmp[MAX_PATH];
    strcpy(tmp, path);
    char* token = tmp;
    if (tmp[0] == '/') token++;

    while (token && *token) {
        char* next = token;
        while (*next && *next != '/') next++;
        if (*next == '/') *next++ = 0;
        else next = 0;

        if (streq(token, ".")) {
            token = next;
            continue;
        }

        if (streq(token, "..")) {
            load_inode(fs, cur);
            cur = fs->inode_cache[cur].parent;
            token = next;
            continue;
        }

        load_inode(fs, cur);

        uint32_t children[128];
        int child_count = get_children(fs, cur, children);

        int found = -1;
        for (int i = 0; i < child_count; i++) {
            int c = children[i];
            load_inode(fs, c);
            if (streq(fs->inode_cache[c].name, token)) {
                found = c;
                break;
            }
        }

        if (found == -1) return -1;

        if (next != 0 && fs->inode_cache[found].type == TYPE_SYMLINK) {
            char target[MAX_PATH];
            strcpy(target, fs->inode_cache[found].symlink_target);

            int resolved;
            if (target[0] == '/') {
                resolved = find_inode(fs, target);
            }
            else {

                char full[MAX_PATH];

                strcpy(full, target);
                resolved = find_inode(fs, full);
            }

            if (resolved < 0) return -1;

            load_inode(fs, resolved);
            if (fs->inode_cache[resolved].type != TYPE_DIR) {
                return -1;
            }
            cur = resolved;
        }
        else {
            cur = found;
        }

        token = next;
    }

    return cur;
}

static int create_inode(fs_instance_t* fs, const char* name, int parent, int type) {
    if (!valid_inode(parent)) return -1;
    load_inode(fs, parent);

    uint32_t children[128];
    int child_count;
    child_count = get_children(fs, parent, children);
    for (int i = 0; i < child_count; i++) {
        int c = children[i];
        if (!valid_inode(c)) continue;
        load_inode(fs, c);
        if (streq(fs->inode_cache[c].name, name)) return -1;
    }

    int id = alloc_inode(fs);
    if (id == -1) return -1;

    inode_t* in = &fs->inode_cache[id];
    int len = 0;
    while (name[len] && len < MAX_FILENAME - 1) {
        in->name[len] = name[len];
        len++;
    }
    in->name[len] = 0;
    in->type = type;
    in->size = 0;
    in->parent = parent;
    in->indirect = -1;
    in->first_child_block = -1;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) in->blocks[i] = -1;

    fs->inode_cache_dirty[id] = 1;
    save_inode(fs, id);

    dir_add_child(fs, parent, id);
    return id;
}

static int read_file_data(fs_instance_t* fs, int inode_num, uint8_t* buffer, uint32_t offset, uint32_t size) {
    load_inode(fs, inode_num);
    inode_t* inode = &fs->inode_cache[inode_num];
    if (offset >= inode->size) return 0;
    if (offset + size > inode->size) size = inode->size - offset;

    uint32_t block_start = offset / BLOCK_SIZE;
    uint32_t block_offset = offset % BLOCK_SIZE;
    uint32_t bytes_read = 0;
    int direct_count = MAX_BLOCKS_PER_FILE;
    int indirect_count = BLOCK_SIZE / 4;
    int total_blocks = direct_count + indirect_count;

    for (uint32_t i = block_start; i < total_blocks && bytes_read < size; i++) {
        uint32_t block_num;

        if (i < direct_count) {

            if (inode->blocks[i] == -1) break;
            block_num = inode->blocks[i];
        }
        else {

            uint32_t indirect_idx = i - direct_count;
            if (inode->indirect == -1) break;

            uint32_t indirect_blocks[128];
            dev_read(fs->dev, DATA_START + inode->indirect, 1, indirect_blocks);
            if (indirect_blocks[indirect_idx] == -1) break;
            block_num = indirect_blocks[indirect_idx];
        }

        uint8_t block_data[BLOCK_SIZE];
        dev_read(fs->dev, DATA_START + block_num, 1, block_data);

        uint32_t copy_start = (i == block_start) ? block_offset : 0;
        uint32_t copy_size = BLOCK_SIZE - copy_start;
        if (copy_size > size - bytes_read) copy_size = size - bytes_read;

        memcpy(buffer + bytes_read, block_data + copy_start, copy_size);
        bytes_read += copy_size;
    }
    return bytes_read;
}

static int write_file_data(fs_instance_t* fs, int inode_num, const uint8_t* buffer, uint32_t offset, uint32_t size) {
    load_inode(fs, inode_num);
    inode_t* inode = &fs->inode_cache[inode_num];
    if (offset + size > inode->size) {
        inode->size = offset + size;
        fs->inode_cache_dirty[inode_num] = 1;
    }

    uint32_t block_start = offset / BLOCK_SIZE;
    uint32_t block_offset = offset % BLOCK_SIZE;
    uint32_t written = 0;
    int direct_count = MAX_BLOCKS_PER_FILE;
    int indirect_count = BLOCK_SIZE / 4;
    int total_blocks = direct_count + indirect_count;

    for (uint32_t i = block_start; i < total_blocks && written < size; i++) {
        uint32_t block_num;

        if (i < direct_count) {

            if (inode->blocks[i] == -1) {
                int nb = alloc_block(fs);
                if (nb == -1) return written;
                inode->blocks[i] = nb;
                fs->inode_cache_dirty[inode_num] = 1;
            }
            block_num = inode->blocks[i];
        }
        else {

            uint32_t indirect_idx = i - direct_count;

            if (inode->indirect == -1) {
                int nb = alloc_block(fs);
                if (nb == -1) return written;
                inode->indirect = nb;
                fs->inode_cache_dirty[inode_num] = 1;

                uint32_t zero[128];
                for (int j = 0; j < 128; j++) zero[j] = -1;
                dev_write(fs->dev, DATA_START + nb, 1, zero);
            }

            uint32_t indirect_blocks[128];
            dev_read(fs->dev, DATA_START + inode->indirect, 1, indirect_blocks);

            if (indirect_blocks[indirect_idx] == -1) {
                int nb = alloc_block(fs);
                if (nb == -1) return written;
                indirect_blocks[indirect_idx] = nb;
                dev_write(fs->dev, DATA_START + inode->indirect, 1, indirect_blocks);
                fs->inode_cache_dirty[inode_num] = 1;
            }
            block_num = indirect_blocks[indirect_idx];
        }

        uint8_t block[BLOCK_SIZE] = { 0 };
        dev_read(fs->dev, DATA_START + block_num, 1, block);

        uint32_t cstart = (i == block_start) ? block_offset : 0;
        uint32_t csize = BLOCK_SIZE - cstart;
        if (csize > size - written) csize = size - written;

        memcpy(block + cstart, buffer + written, csize);
        dev_write(fs->dev, DATA_START + block_num, 1, block);
        written += csize;
    }

    save_inode(fs, inode_num);
    return written;
}

static void split_path(const char* path, char* dir, char* base) {
    int last_slash = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') last_slash = i;
    if (last_slash == -1) {
        dir[0] = '.';
        dir[1] = 0;
        strcpy(base, path);
    }
    else {
        int i;
        if (last_slash == 0) {
            dir[0] = '/';
            dir[1] = 0;
            strcpy(base, path + 1);
        }
        else {
            for (i = 0; i < last_slash; i++) dir[i] = path[i];
            dir[i] = 0;
            strcpy(base, path + last_slash + 1);
        }
    }
}

static fs_instance_t* vfs_resolve(const char* path, const char** rel) {

    if (path[0] != '/') {
        *rel = path;
        return current_fs;
    }

    int best = -1;
    int best_len = 0;

    for (int i = 0; i < vfs_mount_count; i++) {
        const char* mp = vfs_mounts[i].path;
        int len = 0;
        while (mp[len]) len++;

        if (len == 1 && mp[0] == '/') continue;

        if (strncmp(path, mp, len) == 0 &&
            (path[len] == '/' || path[len] == 0)) {
            if (len > best_len) {
                best = i;
                best_len = len;
            }
        }
    }

    if (best != -1) {
        *rel = path + best_len;
        if (**rel == 0) *rel = "/";
        return vfs_mounts[best].fs;
    }

    for (int i = 0; i < vfs_mount_count; i++) {
        if (vfs_mounts[i].path[0] == '/' && vfs_mounts[i].path[1] == 0) {
            *rel = path;
            return vfs_mounts[i].fs;
        }
    }

    *rel = path;
    return current_fs;
}

int fs_create_file(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    char dir[MAX_PATH], base[MAX_FILENAME];
    split_path(rel, dir, base);

    int parent = find_inode(fs, dir);
    if (parent < 0) return 0;

    load_inode(fs, parent);

    uint32_t children[128];
    int child_count;
    child_count = get_children(fs, parent, children);

    for (int i = 0; i < child_count; i++) {
        int c = children[i];
        load_inode(fs, c);
        if (streq(fs->inode_cache[c].name, base))
            return 0;
    }

    int inode_num = create_inode(fs, base, parent, TYPE_FILE);
    return inode_num >= 0;
}

int fs_create_dir(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    char dir[MAX_PATH], base[MAX_FILENAME];
    split_path(rel, dir, base);

    int parent = find_inode(fs, dir);
    if (parent < 0) return 0;

    load_inode(fs, parent);

    if (fs->inode_cache[parent].type != TYPE_DIR)
        return 0;

    return create_inode(fs, base, parent, TYPE_DIR) >= 0;
}

void fs_format(block_device_t* dev) {
    if (!dev) return;

    print("Formatting filesystem...\n");

    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;

    dev_write(dev, SUPERBLOCK_SECTOR, 1, zero);

    int inode_sectors = (MAX_INODES / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < inode_sectors; i++) {
        dev_write(dev, INODE_BITMAP_START + i, 1, zero);
    }

    int block_sectors = (TOTAL_BLOCKS / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < block_sectors; i++) {
        dev_write(dev, BLOCK_BITMAP_START + i, 1, zero);
    }

    int inode_table_sectors = (MAX_INODES * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < inode_table_sectors; i++) {
        dev_write(dev, INODE_START + i, 1, zero);
    }

    inode_t root;
    for (int i = 0; i < MAX_FILENAME; i++) root.name[i] = 0;
    root.name[0] = '/';
    root.type = TYPE_DIR;
    root.parent = 0;
    root.size = 0;
    root.indirect = -1;
    root.first_child_block = -1;

    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++)
        root.blocks[i] = -1;

    uint8_t buf[512] = { 0 };
    memcpy(buf, &root, sizeof(inode_t));
    dev_write(dev, INODE_START, 1, buf);

    uint8_t sb[512];
    for (int i = 0; i < 512; i++) sb[i] = 0;
    sb[0] = 'M';
    sb[1] = 'F';
    sb[2] = 'S';

    dev_write(dev, SUPERBLOCK_SECTOR, 1, sb);

    print("Format complete\n");
}

int fs_format_device(const char* path) {
    block_device_t* dev = block_find(path + 5);

    if (!dev) {
        print("Device not found\n");
        return 0;
    }

    fs_format(dev);
    return 1;
}

int fs_remove_file(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    int id = find_inode(fs, rel);
    if (id < 0) return 0;

    load_inode(fs, id);

    if (fs->inode_cache[id].type != TYPE_FILE && fs->inode_cache[id].type != TYPE_SYMLINK) {
        print("Not a file or symlink\n");
        return 0;
    }

    int parent = fs->inode_cache[id].parent;
    dir_remove_child(fs, parent, id);

    if (fs->inode_cache[id].type == TYPE_FILE) {
        for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
            if (fs->inode_cache[id].blocks[i] != -1) {
                free_block(fs, fs->inode_cache[id].blocks[i]);
                fs->inode_cache[id].blocks[i] = -1;
            }
        }
    }

    free_inode(fs, id);
    return 1;
}

int fs_remove_dir(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    int id = find_inode(fs, rel);
    if (id < 0) {
        print("Dir not found\n");
        return 0;
    }

    load_inode(fs, id);

    if (fs->inode_cache[id].type != TYPE_DIR) {
        print("Not a directory\n");
        return 0;
    }

    uint32_t children[128];
    int child_count = get_children(fs, id, children);

    if (child_count > 0) {
        print("Directory not empty\n");
        return 0;
    }

    int parent = fs->inode_cache[id].parent;
    dir_remove_child(fs, parent, id);

    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
        fs->inode_cache[id].blocks[i] = -1;
    }

    fs->inode_cache[id].type = INODE_FREE;
    fs->inode_cache[id].size = 0;
    fs->inode_cache[id].first_child_block = (uint32_t)-1;

    fs->inode_cache_dirty[id] = 1;
    save_inode(fs, id);

    return 1;
}

int fs_write_file(const char* path, const char* data) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    int inode_num = find_inode(fs, rel);
    if (inode_num < 0) {
        print("File not found\n");
        return 0;
    }

    load_inode(fs, inode_num);
    if (fs->inode_cache[inode_num].type != TYPE_FILE) {
        print("Not a file\n");
        return 0;
    }

    int len = 0;
    while (data[len]) len++;
    int bytes_written = write_file_data(fs, inode_num, (uint8_t*)data, 0, len);
    return (bytes_written == len);
}

void fs_read_file(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return;

    int inode_num = find_inode(fs, rel);
    if (inode_num < 0) {
        print("File not found\n");
        return;
    }

    load_inode(fs, inode_num);
    if (fs->inode_cache[inode_num].type != TYPE_FILE) {
        print("Not a file\n");
        return;
    }

    uint8_t buffer[1024];
    int size = fs->inode_cache[inode_num].size;
    int offset = 0;

    while (offset < size) {
        int bytes = read_file_data(fs, inode_num, buffer, offset, 1024);
        for (int i = 0; i < bytes; i++) print_char(buffer[i]);
        offset += bytes;
    }
    print("\n");
}

void fs_list_dir(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return;

    int dir = find_inode(fs, rel);
    if (dir < 0) {
        print("Not found\n");
        return;
    }

    load_inode(fs, dir);
    if (fs->inode_cache[dir].type != TYPE_DIR) {
        print("Not a directory\n");
        return;
    }

    uint32_t children[128];
    int child_count = get_children(fs, dir, children);

    for (int i = 0; i < child_count; i++) {
        int child = children[i];
        load_inode(fs, child);

        if (fs->inode_cache[child].type == TYPE_DIR) {
            print("DIR  ");
        }
        else if (fs->inode_cache[child].type == TYPE_SYMLINK) {
            print("LINK ");
        }
        else {
            print("FILE ");
        }
        print(fs->inode_cache[child].name);

        if (fs->inode_cache[child].type == TYPE_SYMLINK) {
            print(" -> ");
            print(fs->inode_cache[child].symlink_target);
        }

        print("\n");
    }
}

void fs_cd(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) {
        print("Path not found\n");
        return;
    }

    if (rel[0] == '/' && rel[1] == 0) {
        fs->current_dir = 0;
        current_fs = fs;
        print("Switched to mounted FS root\n");
        return;
    }

    int dir = find_inode(fs, rel);
    if (dir < 0) {
        print("Not found\n");
        return;
    }

    load_inode(fs, dir);

    if (fs->inode_cache[dir].type != TYPE_DIR) {
        print("Not directory\n");
        return;
    }

    fs->current_dir = dir;
    current_fs = fs;
}

void fs_pwd(void) {
    if (!current_fs) {
        print("/\n");
        return;
    }

    const char* mount_point = "/";

    for (int i = 0; i < vfs_mount_count; i++) {
        if (vfs_mounts[i].fs == current_fs) {
            mount_point = vfs_mounts[i].path;
            break;
        }
    }

    print(mount_point);

    if (current_fs->current_dir == 0) {
        print("/\n");
        return;
    }

    int path[MAX_INODES], depth = 0;
    int cur = current_fs->current_dir;

    while (cur != 0) {
        path[depth++] = cur;
        cur = current_fs->inode_cache[cur].parent;
    }

    for (int i = depth - 1; i >= 0; i--) {
        print("/");
        print(current_fs->inode_cache[path[i]].name);
    }

    print("\n");
}

void get_pwd_string(char* buf, int max_len) {
    if (!current_fs) {
        buf[0] = '/';
        buf[1] = 0;
        return;
    }

    const char* mount_point = "/";
    for (int i = 0; i < vfs_mount_count; i++) {
        if (vfs_mounts[i].fs == current_fs) {
            mount_point = vfs_mounts[i].path;
            break;
        }
    }

    int pos = 0;

    const char* mp = mount_point;
    while (*mp && pos < max_len - 1) {
        buf[pos++] = *mp++;
    }

    if (pos > 0 && buf[pos - 1] != '/' && current_fs->current_dir != 0) {
        buf[pos++] = '/';
    }
    else if (pos == 0) {
        buf[pos++] = '/';
    }

    if (current_fs->current_dir == 0) {
        if (pos > 0 && buf[pos - 1] != '/') {
            buf[pos++] = '/';
        }
        buf[pos] = 0;
        return;
    }

    int path[MAX_INODES], depth = 0;
    int cur = current_fs->current_dir;

    while (cur != 0) {
        path[depth++] = cur;
        load_inode(current_fs, cur);
        cur = current_fs->inode_cache[cur].parent;
    }

    for (int i = depth - 1; i >= 0 && pos < max_len - 1; i--) {
        const char* name = current_fs->inode_cache[path[i]].name;
        while (*name && pos < max_len - 1) {
            buf[pos++] = *name++;
        }
        if (i > 0 && pos < max_len - 1) {
            buf[pos++] = '/';
        }
    }

    buf[pos] = 0;
}

int fs_mount(block_device_t* dev, const char* mount_point) {
    if (vfs_mount_count >= MAX_MOUNTS) return 0;

    fs_instance_t* fs = &instances[instance_count];
    fs->dev = dev;

    uint8_t magic[512];
    dev_read(dev, SUPERBLOCK_SECTOR, 1, magic);

    if (magic[0] != 'M' || magic[1] != 'F' ||
        magic[2] != 'S') {
        print("Not MFS\n");
        return 0;
    }

    for (int i = 0; i < MAX_INODES; i++) {
        fs->inode_cache_loaded[i] = 0;
        fs->inode_cache_dirty[i] = 0;
    }

    load_bitmap(fs);
    load_inode_bitmap(fs);

    load_inode(fs, 0);

    fs->current_dir = 0;
    fs->mounted = 1;

    vfs_mounts[vfs_mount_count].fs = fs;

    int i = 0;
    while (mount_point[i] && i < MAX_PATH - 1) {
        vfs_mounts[vfs_mount_count].path[i] = mount_point[i];
        i++;
    }
    vfs_mounts[vfs_mount_count].path[i] = 0;

    vfs_mount_count++;
    instance_count++;

    if (mount_point[0] == '/' && mount_point[1] == 0)
        current_fs = fs;

    return 1;
}

int fs_umount(const char* mount_point) {

    int idx = -1;
    for (int i = 0; i < vfs_mount_count; i++) {
        if (streq(vfs_mounts[i].path, mount_point)) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        print("Not mounted: ");
        print(mount_point);
        print("\n");
        return 0;
    }

    if (streq(mount_point, "/")) {
        print("Cannot unmount root filesystem\n");
        return 0;
    }

    if (current_fs == vfs_mounts[idx].fs) {
        print("Cannot unmount: current directory is on this device\n");
        return 0;
    }

    for (int i = idx; i < vfs_mount_count - 1; i++) {
        vfs_mounts[i] = vfs_mounts[i + 1];
    }
    vfs_mount_count--;

    print("Unmounted ");
    print(mount_point);
    print("\n");
    return 1;
}

static int resolve_symlink_target(fs_instance_t* fs, int inode_num) {
    load_inode(fs, inode_num);
    if (fs->inode_cache[inode_num].type != TYPE_SYMLINK) {
        return inode_num;
    }

    char target[MAX_PATH];
    strcpy(target, fs->inode_cache[inode_num].symlink_target);

    if (target[0] == '/') {
        return find_inode(fs, target);
    }
    else {

        int parent = fs->inode_cache[inode_num].parent;

        char full[MAX_PATH];
        if (parent == 0) {
            full[0] = '/';
            full[1] = 0;
        }
        else {

            int dirs[MAX_INODES];
            int dcount = 0;
            int cur = parent;
            while (cur != 0) {
                dirs[dcount++] = cur;
                load_inode(fs, cur);
                cur = fs->inode_cache[cur].parent;
            }
            int pos = 0;
            for (int i = dcount - 1; i >= 0; i--) {
                load_inode(fs, dirs[i]);
                const char* name = fs->inode_cache[dirs[i]].name;
                while (*name) full[pos++] = *name++;
                full[pos++] = '/';
            }
            full[pos] = 0;
        }
        strcat(full, target);
        return find_inode(fs, full);
    }
}

static int is_block_device(int inode_num) {
    if (!current_fs) return 0;
    load_inode(current_fs, inode_num);
    return current_fs->inode_cache[inode_num].type == TYPE_BLOCK_DEV;
}

static block_device_t* get_device_from_inode(int inode_num) {
    if (!current_fs) return 0;
    load_inode(current_fs, inode_num);
    int major = current_fs->inode_cache[inode_num].blocks[0];
    if (major >= 0 && major < dev_count) {
        return &devices[major];
    }
    return 0;
}

int vfs_create_file(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    return fs_create_file(rel);
}

int vfs_create_dir(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    return fs_create_dir(rel);
}

static int resolve_symlink(fs_instance_t* fs, int inode_num, int depth) {
    if (depth > 10) {
        print("Too many levels of symlinks\n");
        return -1;
    }

    load_inode(fs, inode_num);
    if (fs->inode_cache[inode_num].type != TYPE_SYMLINK) {
        return inode_num;
    }

    char target[MAX_PATH];
    strcpy(target, fs->inode_cache[inode_num].symlink_target);

    int target_inode;
    if (target[0] == '/') {
        target_inode = find_inode(fs, target);
    }
    else {

        int parent = fs->inode_cache[inode_num].parent;

        char parent_path[MAX_PATH];
        char tmp[MAX_PATH];
        int depth2 = 0;
        int cur = parent;

        if (cur == 0) {
            parent_path[0] = '/';
            parent_path[1] = 0;
        }
        else {
            int dirs[MAX_INODES];
            int dcount = 0;
            while (cur != 0) {
                dirs[dcount++] = cur;
                load_inode(fs, cur);
                cur = fs->inode_cache[cur].parent;
            }

            int pos = 0;
            for (int i = dcount - 1; i >= 0; i--) {
                load_inode(fs, dirs[i]);
                const char* name = fs->inode_cache[dirs[i]].name;
                while (*name) parent_path[pos++] = *name++;
                parent_path[pos++] = '/';
            }
            parent_path[pos] = 0;
        }

        char full[MAX_PATH];
        strcpy(full, parent_path);
        strcat(full, target);
        target_inode = find_inode(fs, full);
    }

    if (target_inode < 0) {
        print("Broken symlink\n");
        return -1;
    }

    return resolve_symlink(fs, target_inode, depth + 1);
}

int vfs_write(const char* path, const char* data) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    int inode_num = find_inode(fs, rel);
    if (inode_num < 0) {
        print("File not found\n");
        return 0;
    }

    int real_inode = resolve_symlink(fs, inode_num, 0);
    if (real_inode < 0) return 0;

    if (fs->inode_cache[real_inode].type == TYPE_BLOCK_DEV) {
        int major = fs->inode_cache[real_inode].blocks[0];
        if (major >= 0 && major < dev_count) {
            uint8_t buffer[512];
            int len = 0;
            while (data[len] && len < 512) {
                buffer[len] = data[len];
                len++;
            }
            while (len < 512) buffer[len++] = 0;

            if (devices[major].write(&devices[major], 0, 1, buffer)) {
                return 1;
            }
            else {
                print("Write error\n");
            }
        }
        else {
            print("Invalid device\n");
        }
        return 0;
    }

    if (fs->inode_cache[real_inode].type != TYPE_FILE) {
        print("Not a file\n");
        return 0;
    }

    int len = 0;
    while (data[len]) len++;
    int bytes_written = write_file_data(fs, real_inode, (uint8_t*)data, 0, len);
    return (bytes_written == len);
}

void vfs_read(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return;

    int inode_num = find_inode(fs, rel);
    if (inode_num < 0) {
        print("File not found\n");
        return;
    }

    int real_inode = resolve_symlink(fs, inode_num, 0);
    if (real_inode < 0) return;

    if (fs->inode_cache[real_inode].type == TYPE_BLOCK_DEV) {
        int major = fs->inode_cache[real_inode].blocks[0];
        if (major >= 0 && major < dev_count) {
            uint8_t buffer[512];
            if (devices[major].read(&devices[major], 0, 1, buffer)) {
                for (int i = 0; i < 512; i++) print_char(buffer[i]);
                print("\n");
            }
            else {
                print("Read error\n");
            }
        }
        else {
            print("Invalid device\n");
        }
        return;
    }

    if (fs->inode_cache[real_inode].type != TYPE_FILE) {
        print("Not a file\n");
        return;
    }

    uint8_t buffer[1024];
    int size = fs->inode_cache[real_inode].size;
    int offset = 0;

    while (offset < size) {
        int bytes = read_file_data(fs, real_inode, buffer, offset, 1024);
        for (int i = 0; i < bytes; i++) print_char(buffer[i]);
        offset += bytes;
    }
    print("\n");
}

int vfs_remove_file(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    return fs_remove_file(rel);
}

int vfs_remove_dir(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return 0;

    return fs_remove_dir(rel);
}

void vfs_list(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return;

    fs_list_dir(rel);
}

void vfs_cd(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) {
        print("Path not found\n");
        return;
    }

    if (streq(path, "..") || (rel[0] == '/' && rel[1] == 0 && streq(rel, ".."))) {
        if (fs->current_dir != 0) {

            load_inode(fs, fs->current_dir);
            fs->current_dir = fs->inode_cache[fs->current_dir].parent;
            current_fs = fs;
        }
        else {

            for (int i = 0; i < vfs_mount_count; i++) {
                if (vfs_mounts[i].fs == fs && !streq(vfs_mounts[i].path, "/")) {
                    current_fs = vfs_mounts[0].fs;
                    char mount_path[MAX_PATH];
                    strcpy(mount_path, vfs_mounts[i].path);
                    const char* target = mount_path[0] == '/' ? mount_path + 1 : mount_path;
                    if (target[0] == 0) {
                        current_fs->current_dir = 0;
                    }
                    else {
                        int dir = find_inode(current_fs, target);
                        current_fs->current_dir = (dir >= 0) ? dir : 0;
                    }
                    return;
                }
            }
        }
        return;
    }

    if (rel[0] == '/' && rel[1] == 0) {
        fs->current_dir = 0;
        current_fs = fs;
        return;
    }

    if (rel[0] == 0) {
        fs->current_dir = 0;
        current_fs = fs;
        return;
    }

    int dir = find_inode(fs, rel);
    if (dir < 0) {
        print("Not found\n");
        return;
    }

    int real_dir = resolve_symlink_target(fs, dir);
    if (real_dir < 0) {
        print("Broken symlink\n");
        return;
    }

    load_inode(fs, real_dir);
    if (fs->inode_cache[real_dir].type != TYPE_DIR) {
        print("Not a directory\n");
        return;
    }

    fs->current_dir = real_dir;
    current_fs = fs;
}

int fs_create_symlink(const char* target, const char* linkname) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(linkname, &rel);
    if (!fs) return 0;

    char target_abs[MAX_PATH];
    int target_inode;

    if (target[0] == '/') {
        target_inode = find_inode(fs, target);
    }
    else {

        char* cur_path = NULL;
        char pwd_buf[MAX_PATH];
        get_pwd_string(pwd_buf, MAX_PATH);
        strcpy(target_abs, pwd_buf);
        if (target_abs[strlen(target_abs) - 1] != '/') strcat(target_abs, "/");
        strcat(target_abs, target);
        target_inode = find_inode(fs, target_abs);
    }

    if (target_inode < 0) {
        print("Target does not exist: ");
        print(target);
        print("\n");
        return 0;
    }

    char dir[MAX_PATH], base[MAX_FILENAME];
    split_path(rel, dir, base);

    int parent = find_inode(fs, dir);
    if (parent < 0) return 0;

    load_inode(fs, parent);

    uint32_t children[128];
    int child_count = get_children(fs, parent, children);
    for (int i = 0; i < child_count; i++) {
        int c = children[i];
        load_inode(fs, c);
        if (streq(fs->inode_cache[c].name, base)) {
            print("Already exists\n");
            return 0;
        }
    }

    int id = alloc_inode(fs);
    if (id < 0) return 0;

    inode_t* in = &fs->inode_cache[id];
    strcpy(in->name, base);
    in->type = TYPE_SYMLINK;
    in->size = 0;
    in->parent = parent;
    in->first_child_block = -1;
    in->indirect = -1;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) in->blocks[i] = -1;

    strcpy(in->symlink_target, target);

    fs->inode_cache_dirty[id] = 1;
    save_inode(fs, id);

    dir_add_child(fs, parent, id);
    return 1;
}

int fs_readlink(const char* path, char* buf, int buf_size) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return -1;

    int id = find_inode(fs, rel);
    if (id < 0) return -1;

    load_inode(fs, id);
    if (fs->inode_cache[id].type != TYPE_SYMLINK) return -1;

    strcpy(buf, fs->inode_cache[id].symlink_target);
    return 0;
}

int vfs_create_symlink(const char* target, const char* linkname) {
    return fs_create_symlink(target, linkname);
}

void vfs_readlink(const char* path) {
    char target[MAX_PATH];
    if (fs_readlink(path, target, MAX_PATH) == 0) {
        print(target);
        print("\n");
    }
    else {
        print("Not a symlink\n");
    }
}

void fs_update_devices(void) {
    if (!current_fs) return;

    int dev_inode = find_inode(current_fs, "/dev");
    if (dev_inode < 0) {
        fs_create_dir("/dev");
        dev_inode = find_inode(current_fs, "/dev");
        if (dev_inode < 0) {
            print("Failed to create /dev\n");
            return;
        }
    }

    uint32_t children[256];
    int child_count = get_children(current_fs, dev_inode, children);

    for (int i = 0; i < child_count; i++) {
        int child = children[i];
        load_inode(current_fs, child);

        if (current_fs->inode_cache[child].type == TYPE_BLOCK_DEV) {
            dir_remove_child(current_fs, dev_inode, child);
            free_inode(current_fs, child);
        }
    }

    for (int i = 0; i < dev_count; i++) {
        int id = alloc_inode(current_fs);
        if (id < 0) continue;
        inode_t* in = &current_fs->inode_cache[id];
        strcpy(in->name, devices[i].name);
        in->type = TYPE_BLOCK_DEV;
        in->size = 0;
        in->parent = dev_inode;
        in->first_child_block = -1;
        in->indirect = -1;
        for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) in->blocks[j] = -1;
        in->blocks[0] = i;
        current_fs->inode_cache_dirty[id] = 1;
        save_inode(current_fs, id);
        dir_add_child(current_fs, dev_inode, id);
    }

    for (int i = 0; i < part_count; i++) {
        int id = alloc_inode(current_fs);
        if (id < 0) continue;
        inode_t* in = &current_fs->inode_cache[id];
        strcpy(in->name, partitions[i].name);
        in->type = TYPE_BLOCK_DEV;
        in->size = 0;
        in->parent = dev_inode;
        in->first_child_block = -1;
        in->indirect = -1;
        for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) in->blocks[j] = -1;
        in->blocks[0] = dev_count + i;
        current_fs->inode_cache_dirty[id] = 1;
        save_inode(current_fs, id);
        dir_add_child(current_fs, dev_inode, id);
    }
}

void fs_remove_device(const char* devname) {
    if (!current_fs) return;

    int dev_inode = find_inode(current_fs, "/dev");
    if (dev_inode < 0) return;

    uint32_t children[256];
    int child_count = get_children(current_fs, dev_inode, children);

    for (int i = 0; i < child_count; i++) {
        int child = children[i];
        load_inode(current_fs, child);
        if (streq(current_fs->inode_cache[child].name, devname)) {

            dir_remove_child(current_fs, dev_inode, child);

            free_inode(current_fs, child);
            break;
        }
    }
}

int fs_get_file_size(const char* path) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return -1;

    int inode_num = find_inode(fs, rel);
    if (inode_num < 0) return -1;

    load_inode(fs, inode_num);
    if (fs->inode_cache[inode_num].type != TYPE_FILE) return -1;

    return fs->inode_cache[inode_num].size;
}

int fs_read_file_to_buf(const char* path, char* buf, int max_len) {
    const char* rel;
    fs_instance_t* fs = vfs_resolve(path, &rel);
    if (!fs) return -1;

    int inode_num = find_inode(fs, rel);
    if (inode_num < 0) return -1;

    load_inode(fs, inode_num);
    if (fs->inode_cache[inode_num].type != TYPE_FILE) return -1;

    int size = fs->inode_cache[inode_num].size;
    if (size > max_len) size = max_len;

    int offset = 0;
    while (offset < size) {
        int bytes = read_file_data(fs, inode_num, (uint8_t*)buf + offset, offset, size - offset);
        if (bytes <= 0) break;
        offset += bytes;
    }
    return offset;
}

void fs_init(void) {
    block_device_t* root_dev = NULL;

    for (int i = 0; i < part_count; i++) {
        if (streq(partitions[i].name, "hda1")) {
            root_dev = &part_devices[i];
            break;
        }
    }

    if (!root_dev) {
        print("ERROR: No root partition /dev/hda1 found!\n");
        print("Please create partition with: mkpart /dev/hda 1 <size>\n");
        print("System will reboot in 5 seconds...\n");

        for (volatile int i = 0; i < 5000000; i++) {
            __asm__ volatile ("pause");
        }
        reboot();
        return;
    }

    uint8_t magic[512];
    root_dev->read(root_dev, SUPERBLOCK_SECTOR, 1, magic);

    if (magic[0] == 'M' && magic[1] == 'F' && magic[2] == 'S') {
        if (fs_mount(root_dev, "/")) {
            current_fs = &instances[0];
        }
        else {
            print("Failed to mount root FS\n");
            reboot();
        }
    }
    else {
        print("No filesystem on /dev/hda1, formatting...\n");
        fs_format(root_dev);
        if (fs_mount(root_dev, "/")) {
            current_fs = &instances[0];
        }
        else {
            print("Format failed\n");
            reboot();
        }
    }

    fs_update_devices();
}