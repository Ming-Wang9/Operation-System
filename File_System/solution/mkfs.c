#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include "wfs.h"
#include <getopt.h>

int roundup(int num, int factor) {
    return num % factor == 0 ? num : num + (factor - (num % factor));
}

void usage(char *name) {
    printf("Usage: %s -r <raid mode> -d <disk image file> -d <disk image file> ... -i <inode count> -b <data block count>\n", name);
    printf("\t-r RAID mode: 0 (striping) or 1 (mirroring)\n");
    printf("\t-d Specifies a disk file (can be used multiple times)\n");
    printf("\t-i Number of inodes in the filesystem (rounded to nearest multiple of 32)\n");
    printf("\t-b Number of data blocks in the filesystem (rounded to nearest multiple of 32)\n");
}

int main(int argc, char **argv) {
    int raid_mode = -1;
    char *disk_files[10];
    int disk_count = 0;
    int inodeCount = 0, dataCount = 0;

    int op;
    while ((op = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (op) {
            case 'r':
                raid_mode = atoi(optarg);
                if (raid_mode != 0 && raid_mode != 1) {
                    fprintf(stderr, "Invalid RAID mode. Use 0 (striping) or 1 (mirroring).\n");
                    usage(argv[0]);
                    return 1;
                }
                break;
            case 'd':
                if (disk_count >= 10) {
                    fprintf(stderr, "Too many disk files specified (maximum 10).\n");
                    return 1;
                }
                disk_files[disk_count++] = optarg;
                break;
            case 'i':
                inodeCount = roundup(atoi(optarg), 32);
                break;
            case 'b':
                dataCount = roundup(atoi(optarg), 32);
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (raid_mode == -1 || disk_count == 0 || inodeCount == 0 || dataCount == 0) {
        usage(argv[0]);
        return 1;
    }

    if (raid_mode == 1 && disk_count < 2) {
        fprintf(stderr, "RAID 1 requires at least two disks.\n");
        return 1;
    }

    // Calculate total size for the filesystem
    int bitmap_size = inodeCount / 8 + dataCount / 8;
    int inode_region_size = inodeCount * BLOCK_SIZE;
    int data_region_size = dataCount * BLOCK_SIZE;
    int fs_size = sizeof(struct wfs_sb) + bitmap_size + inode_region_size + data_region_size;

    // Calculate total available disk space
    long long total_disk_space = 0;
    struct stat st;
    for (int i = 0; i < disk_count; i++) {
        if (stat(disk_files[i], &st) < 0) {
            perror("stat");
            return 1; // Stat failure
        }
        total_disk_space += st.st_size;
    }

    // Check if enough space is available
    if ((raid_mode == 0 && fs_size > (total_disk_space / disk_count)) ||  // RAID 0
        (raid_mode == 1 && fs_size > st.st_size)) {                      // RAID 1
        fprintf(stderr, "Requested blocks and inodes exceed available disk space.\n");
        return -1; // Fail with correct exit code
    }

    // Open and map each disk file
    int fd[disk_count];
    void *mapped[disk_count];
    for (int i = 0; i < disk_count; i++) {
        fd[i] = open(disk_files[i], O_RDWR);
        if (fd[i] < 0) {
            perror("open");
            return 1;
        }
        mapped[i] = mmap(NULL, fs_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd[i], 0);
        if (mapped[i] == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
        memset(mapped[i], 0, fs_size);
    }

    // Initialize the superblock
    struct wfs_sb *superBlock = (struct wfs_sb *) mapped[0];
    superBlock->num_inodes = inodeCount;
    superBlock->num_data_blocks = dataCount;
    superBlock->i_bitmap_ptr = sizeof(struct wfs_sb);
    superBlock->d_bitmap_ptr = superBlock->i_bitmap_ptr + inodeCount / 8;
    superBlock->i_blocks_ptr = roundup(superBlock->d_bitmap_ptr + dataCount / 8, BLOCK_SIZE);
    superBlock->d_blocks_ptr = roundup(superBlock->i_blocks_ptr + inodeCount * BLOCK_SIZE, BLOCK_SIZE);
    superBlock->raid_mode = raid_mode; 
    superBlock->disk_count = disk_count; 

    // Allocate root inode
    char *i_map = (char *) mapped[0] + superBlock->i_bitmap_ptr;
    *i_map |= 1;  // Mark root inode as allocated
    struct wfs_inode *rootInode = (struct wfs_inode *) ((char *) mapped[0] + superBlock->i_blocks_ptr);
    rootInode->mode = S_IFDIR | 0755;
    rootInode->uid = getuid();
    rootInode->gid = getgid();
    rootInode->size = 0;
    rootInode->nlinks = 2;
    rootInode->atim = rootInode->mtim = rootInode->ctim = time(NULL);

    // Pre-populate root inode's directory entries (., ..)
    struct wfs_dentry *root_dir = (struct wfs_dentry *) ((char *) mapped[0] + superBlock->d_blocks_ptr);
    strcpy(root_dir[0].name, ".");
    root_dir[0].num = 0; // Root inode
    strcpy(root_dir[1].name, "..");
    root_dir[1].num = 0; // Parent of root is root itself

    // Mirror metadata for both RAID 1 and RAID 0 (metadata is always mirrored)
    for (int i = 1; i < disk_count; i++) {
        memcpy(mapped[i], mapped[0], fs_size); // Copy the entire filesystem region
    }

    // Clean up
    for (int i = 0; i < disk_count; i++) {
        munmap(mapped[i], fs_size);
        close(fd[i]);
    }

    return 0;
}