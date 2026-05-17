
#ifndef FS_H
#define FS_H

#include <stdint.h>
#include "block.h"

#define MAX_PATH 256
#define MAX_FILENAME 32
#define BLOCK_SIZE          512
#define TOTAL_BLOCKS        4096
#define INODE_START         1
#define INODES_PER_SECTOR   (BLOCK_SIZE / INODE_SIZE)
#define MAX_INODES          256
#define BLOCK_BITMAP_START  (INODE_START + (MAX_INODES * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE)
#define INODE_BITMAP_START (BLOCK_BITMAP_START + 1)
#define DATA_START          (BLOCK_BITMAP_START + (TOTAL_BLOCKS + 7) / 8 / BLOCK_SIZE + 1)
#define MAX_BLOCKS_PER_FILE 12
#define INDIRECT_BLOCK (MAX_BLOCKS_PER_FILE - 1)

#define SUPERBLOCK_SECTOR 0

#define INODE_FREE 0
#define INODE_FILE 1
#define INODE_DIR  2

#define MAX_MOUNTS 8

typedef enum {
    TYPE_FILE,
    TYPE_DIR,
    TYPE_SYMLINK,
    TYPE_BLOCK_DEV
} NodeType;

typedef struct {
    char name[MAX_FILENAME];
    NodeType type;
    uint32_t size;
    uint32_t parent;
    uint32_t first_child_block;
    uint32_t blocks[12];
    uint32_t indirect;
    char symlink_target[MAX_PATH];
} __attribute__((packed)) inode_t;

#define INODE_SIZE 512

extern block_device_t* root_device;

extern void fs_init(void);
extern int fs_create_file(const char* name);
extern int fs_create_dir(const char* name);
extern int fs_write_file(const char* path, const char* data);
extern void fs_read_file(const char* path);
extern void fs_list_dir(const char* path);
extern void fs_cd(const char* path);
extern void fs_cat(const char* path);
extern void fs_pwd(void);
extern void get_pwd_string(char* buf, int max_len);
int fs_remove_file(const char* path);
int fs_remove_dir(const char* path);
int fs_mount(block_device_t* dev, const char* mount_point);
int fs_umount(const char* mount_point);
int vfs_create_file(const char* path);
int vfs_create_dir(const char* path);
int vfs_write(const char* path, const char* data);
void vfs_read(const char* path);
int vfs_remove_file(const char* path);
int vfs_remove_dir(const char* path);
void vfs_list(const char* path);
void vfs_cd(const char* path);
void fs_format(block_device_t* dev);
int fs_format_device(const char* path);
void fs_remove_device(const char* devname);
int vfs_create_symlink(const char* target, const char* linkname);
void vfs_readlink(const char* path);
int fs_get_file_size(const char* path);
int fs_read_file_to_buf(const char* path, char* buf, int max_len);

extern int part_count;
extern partition_t partitions[MAX_PARTITIONS * MAX_BLOCK_DEVICES];
extern block_device_t part_devices[MAX_PARTITIONS * MAX_BLOCK_DEVICES];

#endif