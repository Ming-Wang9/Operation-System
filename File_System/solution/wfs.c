#define FUSE_USE_VERSION 30
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "wfs.h"

#define OK 0

// Globals
int iCount, dCount;
char *memStart;
struct wfs_sb *sb;
char *inodeMap;
char *inodeStart;
char *dataMap;
char *dataStart;

int disk_count = 0;
int *fds = NULL;
char **disk_maps = NULL;

// Helper function to parse path
int parsePath (const char* path) {
    char *dup = strdup(path);
    if (dup == NULL) {
        perror("strdup");
    }

    char *tok = strtok(dup,"/");
    int iNodeIndex = 0;
    while (tok) {
        struct wfs_inode *curr = (struct wfs_inode *) (inodeStart + iNodeIndex * BLOCK_SIZE);
        if (!(curr->mode & S_IFDIR)) {
            free(dup);
            return -1;
        }

        int found = 0;
        int blockIter = 0;
        while (curr->blocks[blockIter] != 0 && blockIter < IND_BLOCK) {
            struct wfs_dentry *entries = (struct wfs_dentry*)(memStart + curr->blocks[blockIter]);
            int k = -1;
            while (entries->name[0] != 0 && ++k < BLOCK_SIZE / sizeof(struct wfs_dentry)) {
                if (strcmp(entries->name, tok) == 0) {
                    iNodeIndex = entries->num;
                    found = 1;
                    break;
                }
                entries++;
            }
            if (found) break;
            blockIter++;
        }

        if (!found) {
            free(dup);
            return -1;
        }
        tok = strtok(NULL,"/");
    }
    free(dup);
    return iNodeIndex;
}

void debugSignal(int signal) {
    if (signal == SIGUSR1) {
        printf("Inode Map: ");
        for (int i=0; i<sb->num_inodes/8; i++) {
            printf("%x ", (int) *(inodeMap + i));
        }
        printf("\n");

        printf("Data Map: ");
        for (int i=0; i<sb->num_data_blocks/8; i++) {
            printf("%x ", (int) *(dataMap + i));
        }
        printf("\n");
    }
}

int findAndAllocFromMap (char *bitmap, int len) {
    for (int i=0; i<len; i++) {
        char *byte_off = (bitmap + i/8);
        int bit_off = i % 8;
        int bit = (*byte_off >> bit_off) & 1;
        if(!bit) {
            *byte_off |= 1 << bit_off;
            return i;
        }
    }
    return -1;
}

void freeBitFromMap(char *bitmap, int index) {
    char *byte_off = (bitmap + index / 8);
    int bit_off = index % 8;
    *byte_off &= ~(1 << bit_off);
}

// Since metadata must be mirrored in both RAID 0 and RAID 1 if multiple disks, we just check disk_count > 1
void replicate_dataMap() {
    if (disk_count > 1) {
        off_t dataMapOffset = dataMap - memStart;
        size_t dataMapSize = sb->num_data_blocks / 8;
        for (int d = 1; d < disk_count; d++) {
            memcpy(disk_maps[d] + dataMapOffset, memStart + dataMapOffset, dataMapSize);
        }
    }
}

void replicate_block(off_t blockAddr) {
    if (disk_count > 1) {
        for (int d = 1; d < disk_count; d++) {
            memcpy(disk_maps[d] + blockAddr, memStart + blockAddr, BLOCK_SIZE);
        }
    }
}

void replicate_partial_block(off_t start, size_t len, const char *src) {
    if (disk_count > 1) {
        for (int d = 1; d < disk_count; d++) {
            memcpy(disk_maps[d] + start, src, len);
        }
    }
}

void replicate_inode(struct wfs_inode *inode) {
    if (disk_count > 1) {
        off_t inodeOff = (char*)inode - memStart;
        for (int d = 1; d < disk_count; d++) {
            memcpy(disk_maps[d] + inodeOff, inode, BLOCK_SIZE);
        }
    }
}

void parseParentChild (const char* path, char* child, char* parent) {
    char *copy = strdup(path);
    if (copy == NULL) {
        perror("strdup");
        exit(2);
    }

    int len = strlen(copy);
    child[0] = '\0';
    parent[0] = '\0';

    for (int i=len-1; i>=0; i--) {
        if (copy[i] == '/') {
            if (i == len - 1) {
                free(copy);
                return;
            }
            copy[i] = '\0';
            strncpy(parent, copy, MAX_NAME);
            strncpy(child, copy + i + 1, MAX_NAME);
            break;
        }
    }
    free(copy);
}

int handleRemove(const char* path, int isDir) {
    int inodeIndex = parsePath(path);
    if (inodeIndex < 0) {
        return -ENOENT;
    }

    struct wfs_inode *inode = (struct wfs_inode *)(inodeStart + inodeIndex * BLOCK_SIZE);
    if (isDir && inode->size > 0) {
        return -ENOTEMPTY;
    }

    char curr[MAX_NAME];
    char parentPath[MAX_NAME];
    parseParentChild(path, curr, parentPath);

    int parentInodeIndex = parsePath(parentPath);
    if (parentInodeIndex < 0) {
        return -ENOENT;
    }

    struct wfs_inode *parentInode = (struct wfs_inode *)(inodeStart + parentInodeIndex * BLOCK_SIZE);

    if (isDir) {
        parentInode->nlinks--;
    }

    // Locate and remove directory entry
    int blockIter = 0, index = -1;
    while (parentInode->blocks[blockIter] != 0 && blockIter < IND_BLOCK) {
        struct wfs_dentry *entries = (struct wfs_dentry *)(memStart + parentInode->blocks[blockIter]);
        for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
            if (strcmp(entries[i].name, curr) == 0) {
                index = i;
                goto found;
            }
        }
        blockIter++;
    }
    return -ENOENT; // Entry not found

found:
    parentInode->size -= sizeof(struct wfs_dentry);

    int lastBlock = parentInode->size / BLOCK_SIZE;
    int lastOffset = parentInode->size % BLOCK_SIZE;

    if (lastBlock == blockIter && lastOffset == index * sizeof(struct wfs_dentry)) {
        memset(memStart + parentInode->blocks[blockIter] + index * sizeof(struct wfs_dentry), 0, sizeof(struct wfs_dentry));
    } else {
        struct wfs_dentry *lastEntry = (struct wfs_dentry *)(memStart + parentInode->blocks[lastBlock] + lastOffset);
        memcpy(memStart + parentInode->blocks[blockIter] + index * sizeof(struct wfs_dentry), lastEntry, sizeof(struct wfs_dentry));
        memset(lastEntry, 0, sizeof(struct wfs_dentry));
    }

    // Free file's data blocks (direct and indirect)
    if (inode->blocks[IND_BLOCK]) {
        off_t *indirectBlocks = (off_t *)(memStart + inode->blocks[IND_BLOCK]);
        for (int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++) {
            if (indirectBlocks[i] != 0) {
                off_t dataBlockAddr = indirectBlocks[i];
                memset(memStart + dataBlockAddr, 0, BLOCK_SIZE); // Zero out the block
                freeBitFromMap(dataMap, (dataBlockAddr - sb->d_blocks_ptr) / BLOCK_SIZE);

                // Replicate the zeroed block to RAID 1 disks
                if (disk_count > 1 && sb->raid_mode == 1) {
                    for (int d = 1; d < disk_count; d++) {
                        memcpy(disk_maps[d] + dataBlockAddr, memStart + dataBlockAddr, BLOCK_SIZE);
                    }
                }
            }
        }

        // Zero out and free the indirect block itself
        off_t indirectBlockAddr = inode->blocks[IND_BLOCK];
        memset(memStart + indirectBlockAddr, 0, BLOCK_SIZE);
        freeBitFromMap(dataMap, (indirectBlockAddr - sb->d_blocks_ptr) / BLOCK_SIZE);

        // Replicate the zeroed indirect block to RAID 1 disks
        if (disk_count > 1 && sb->raid_mode == 1) {
            for (int d = 1; d < disk_count; d++) {
                memcpy(disk_maps[d] + indirectBlockAddr, memStart + indirectBlockAddr, BLOCK_SIZE);
            }
        }
    }

    // Free direct blocks
    for (int i = 0; i < IND_BLOCK; i++) {
        if (inode->blocks[i]) {
            off_t dataBlockAddr = inode->blocks[i];
            memset(memStart + dataBlockAddr, 0, BLOCK_SIZE); // Zero out the block
            freeBitFromMap(dataMap, (dataBlockAddr - sb->d_blocks_ptr) / BLOCK_SIZE);

            // Replicate the zeroed block to RAID 1 disks
            if (disk_count > 1 && sb->raid_mode == 1) {
                for (int d = 1; d < disk_count; d++) {
                    memcpy(disk_maps[d] + dataBlockAddr, memStart + dataBlockAddr, BLOCK_SIZE);
                }
            }
        }
    }

    // Zero out and free the inode itself
    memset(inode, 0, BLOCK_SIZE);
    freeBitFromMap(inodeMap, inodeIndex);

    // Replicate changes (metadata)
    if (disk_count > 1) {
        off_t inodeMapOffset = inodeMap - memStart;
        size_t inodeMapSize = iCount / 8;

        off_t dataMapOffset = dataMap - memStart;
        size_t dataMapSize = dCount / 8;

        off_t parentOff = (char *)parentInode - memStart;
        off_t inodeOff = (char *)inode - memStart;
        off_t dirBlockOff = parentInode->blocks[blockIter];
        off_t lastBlockOff = parentInode->blocks[lastBlock];

        for (int d = 1; d < disk_count; d++) {
            // Replicate inodeMap (always metadata)
            memcpy(disk_maps[d] + inodeMapOffset, memStart + inodeMapOffset, inodeMapSize);

            // Replicate dataMap (only in RAID 1 mode)
            if (sb->raid_mode == 1) {
                memcpy(disk_maps[d] + dataMapOffset, memStart + dataMapOffset, dataMapSize);
            }

            // Replicate parent inode
            memcpy(disk_maps[d] + parentOff, parentInode, BLOCK_SIZE);

            // Replicate directory blocks
            memcpy(disk_maps[d] + dirBlockOff, memStart + dirBlockOff, BLOCK_SIZE);
            if (lastBlock != blockIter) {
                memcpy(disk_maps[d] + lastBlockOff, memStart + lastBlockOff, BLOCK_SIZE);
            }

            // Replicate zeroed inode
            memcpy(disk_maps[d] + inodeOff, memStart + inodeOff, BLOCK_SIZE);
        }
    }

    return OK;
}

int wfs_getattr(const char* path, struct stat* stbuf) {
    int inodeIndex = parsePath(path);
    if (inodeIndex < 0) return -ENOENT;

    struct wfs_inode *inode = (struct wfs_inode *)(inodeStart + inodeIndex*BLOCK_SIZE);
    // Update atime
    inode->atim = time(NULL);

    // Replicate inode changes in RAID 1
    if (disk_count > 1 && sb->raid_mode == 1) {
        replicate_inode(inode);
    }

    // Fill stbuf with inode metadata
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_mode = inode->mode;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_atim.tv_sec = inode->atim;
    stbuf->st_ctim.tv_sec = inode->ctim;
    stbuf->st_mtim.tv_sec = inode->mtim;
    stbuf->st_size = inode->size;
    stbuf->st_ino = inode->num;

    return OK;
}

int wfs_mknod (const char* path, mode_t mode, dev_t rdev) {
    if (parsePath(path) >= 0) {
        return -EEXIST;
    }

    char name[MAX_NAME];
    char parentPath[MAX_NAME];
    parseParentChild(path, name, parentPath);

    if (name[0] == '\0') {
        return -EBADF;
    }

    int parentInodeIndex = parsePath(parentPath);
    if (parentInodeIndex < 0) {
        return -ENOENT;
    }

    int index = findAndAllocFromMap(inodeMap, iCount);
    if (index < 0) {
        return -ENOSPC;
    }

    struct wfs_inode *parentInode = (struct wfs_inode *) (inodeStart + parentInodeIndex * BLOCK_SIZE);
    int blockNum = parentInode->size / BLOCK_SIZE;
    int off = parentInode->size % BLOCK_SIZE;

    parentInode->mtim = time(NULL);
    parentInode->atim = time(NULL);

    if (!off && !parentInode->blocks[blockNum]) {
       if (blockNum == IND_BLOCK) {
           freeBitFromMap(inodeMap, index);
           return -ENOSPC;
       }

       int ind = findAndAllocFromMap(dataMap, dCount);
       if (ind < 0) {
           freeBitFromMap(inodeMap, index);
           return -ENOSPC;
       }
       parentInode->blocks[blockNum] = sb->d_blocks_ptr + BLOCK_SIZE * ind;
    }

    char *dirBlock = (char*)memStart + parentInode->blocks[blockNum] + off;
    struct wfs_dentry *loc = (struct wfs_dentry *)dirBlock;
    strncpy(loc->name, name, MAX_NAME);
    loc->num = index;

    parentInode->mtim = time(NULL);
    parentInode->size += sizeof(struct wfs_dentry);

    struct wfs_inode *node = (struct wfs_inode *) (inodeStart + BLOCK_SIZE * index);
    node->num = index;
    node->mode = mode;
    node->uid = getuid();
    node->gid = getgid();
    node->size = 0;
    node->nlinks = 1;

    time_t amct = time(NULL);
    node->atim = amct;
    node->mtim = amct;
    node->ctim = amct;

    if (mode & S_IFDIR) {
        parentInode->nlinks++;
    }

    // Replicate metadata if multiple disks
    if (disk_count > 1) {
        off_t inodeMapOffset = inodeMap - memStart;
        size_t inodeMapSize = iCount / 8;

        off_t dataMapOffset = dataMap - memStart;
        size_t dataMapSize = dCount / 8;

        off_t parentOff = (char*)parentInode - memStart;
        off_t childOff = (char*)node - memStart;
        off_t dirBlockOff = parentInode->blocks[blockNum];

        for (int d = 1; d < disk_count; d++) {
            // Inode bitmap is metadata, always replicate
            memcpy(disk_maps[d] + inodeMapOffset, memStart + inodeMapOffset, inodeMapSize);

            // Data bitmap (dataMap) is only replicated in RAID 1 mode
            if (sb->raid_mode == 1) {
                memcpy(disk_maps[d] + dataMapOffset, memStart + dataMapOffset, dataMapSize);
            }

            // Parent inode, directory block, and child's inode are metadata, always replicate
            memcpy(disk_maps[d] + parentOff, parentInode, BLOCK_SIZE);
            memcpy(disk_maps[d] + dirBlockOff, memStart + dirBlockOff, BLOCK_SIZE);
            memcpy(disk_maps[d] + childOff, node, BLOCK_SIZE);
        }
    }

    return OK;
}

int wfs_mkdir (const char* path, mode_t mode) {
    return wfs_mknod(path, mode | S_IFDIR, 0);
}

int wfs_unlink (const char* path) {
    return handleRemove(path, 0);
}

int wfs_rmdir (const char* path) {
    return handleRemove(path, 1);
}

int wfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    int inodeIndex = parsePath(path);
    if (inodeIndex < 0) return -ENOENT;

    struct wfs_inode *inode = (struct wfs_inode *)(inodeStart + BLOCK_SIZE * inodeIndex);
    if (offset >= inode->size) return 0;

    // Update atime
    inode->atim = time(NULL);
    // If RAID1 or RAID1v mode (we treat mode==1 as RAID1/1v), replicate inode after atime change
    if (disk_count > 1 && sb->raid_mode == 1) {
        replicate_inode(inode);
    }

    int bytesRead = 0;
    while (bytesRead < size && bytesRead + offset < inode->size) {
        off_t curOffset = bytesRead + offset;
        int blockIndex = curOffset / BLOCK_SIZE;
        int blockOff = curOffset % BLOCK_SIZE;

        off_t blockAddr = 0;
        if (blockIndex >= IND_BLOCK) {
            if (!inode->blocks[IND_BLOCK]) break;  // No indirect block allocated
            int indirectIndex = blockIndex - IND_BLOCK;
            off_t *indirectBlock = (off_t *)(memStart + inode->blocks[IND_BLOCK]);
            blockAddr = indirectBlock[indirectIndex];
            if (!blockAddr) break; // No block allocated
        } else {
            blockAddr = inode->blocks[blockIndex];
            if (!blockAddr) break; // No block allocated
        }

        // Read from all disks into blockBuf
        char blockBuf[disk_count][BLOCK_SIZE];
        for (int d = 0; d < disk_count; d++) {
            memcpy(blockBuf[d], disk_maps[d] + blockAddr, BLOCK_SIZE);
        }

        // Majority voting logic
        int bestDisk = 0;
        int bestCount = 1;
        // Find the block that has the highest count of matching replicas
        for (int d = 0; d < disk_count; d++) {
            int count = 1;
            for (int d2 = d+1; d2 < disk_count; d2++) {
                if (memcmp(blockBuf[d], blockBuf[d2], BLOCK_SIZE) == 0) {
                    count++;
                }
            }
            if (count > bestCount) {
                bestCount = count;
                bestDisk = d;
            } else if (count == bestCount && d < bestDisk) {
                // tie break with lowest disk index
                bestDisk = d;
            }
        }

        // Repair any corrupted disks if found
        for (int d = 0; d < disk_count; d++) {
            if (d != bestDisk && memcmp(blockBuf[bestDisk], blockBuf[d], BLOCK_SIZE) != 0) {
                // Write the correct block to the corrupted disk
                memcpy(disk_maps[d] + blockAddr, blockBuf[bestDisk], BLOCK_SIZE);
            }
        }

        // Serve the correct data from bestDisk
        int chunk = size - bytesRead;
        int available = BLOCK_SIZE - blockOff;
        if (chunk > available) chunk = available;

        memcpy(buf + bytesRead, blockBuf[bestDisk] + blockOff, chunk);
        bytesRead += chunk;
    }

    return bytesRead;
}

int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int inodeIndex = parsePath(path);
    if (inodeIndex < 0) {
        return -ENOENT;
    }

    struct wfs_inode *inode = (struct wfs_inode *)(inodeStart + BLOCK_SIZE * inodeIndex);
    inode->atim = time(NULL);
    inode->mtim = time(NULL);

    int bytesWritten = 0;

    while (bytesWritten < size) {
        off_t curOffset = bytesWritten + offset;
        int blockOff;
        char *block;
        off_t *actualBlockOff = NULL;
        char *indirectBlock = NULL;
        int useIndirect = (curOffset >= IND_BLOCK * BLOCK_SIZE);

        if (useIndirect) {
            int indirectOff = curOffset - IND_BLOCK * BLOCK_SIZE;
            int byteIndex = indirectOff / BLOCK_SIZE;
            blockOff = indirectOff % BLOCK_SIZE;

            if (byteIndex >= BLOCK_SIZE / sizeof(off_t)) {
                break;
            }

            if (!inode->blocks[IND_BLOCK]) {
                int ind = findAndAllocFromMap(dataMap, dCount);
                if (ind < 0) break;
                inode->blocks[IND_BLOCK] = sb->d_blocks_ptr + BLOCK_SIZE * ind;

                // Only replicate dataMap if RAID 1
                if (disk_count > 1 && sb->raid_mode == 1) {
                    replicate_dataMap();
                }

                replicate_block(inode->blocks[IND_BLOCK]);
            }

            indirectBlock = memStart + inode->blocks[IND_BLOCK];
            actualBlockOff = (off_t *)(indirectBlock + sizeof(off_t) * byteIndex);

            if (!*actualBlockOff) {
                int ind = findAndAllocFromMap(dataMap, dCount);
                if (ind < 0) break;
                *actualBlockOff = sb->d_blocks_ptr + BLOCK_SIZE * ind;

                if (disk_count > 1 && sb->raid_mode == 1) {
                    replicate_dataMap();
                }

                replicate_block(inode->blocks[IND_BLOCK]);
            }

            block = memStart + *actualBlockOff;
        } else {
            int blockIndex = curOffset / BLOCK_SIZE;
            blockOff = curOffset % BLOCK_SIZE;

            if (!inode->blocks[blockIndex]) {
                int ind = findAndAllocFromMap(dataMap, dCount);
                if (ind < 0) break;
                inode->blocks[blockIndex] = sb->d_blocks_ptr + BLOCK_SIZE * ind;

                // Only replicate dataMap if RAID 1
                if (disk_count > 1 && sb->raid_mode == 1) {
                    replicate_dataMap();
                }
            }

            block = memStart + inode->blocks[blockIndex];
        }

        int spaceInBlock = BLOCK_SIZE - blockOff;
        int remain = size - bytesWritten;
        int toWrite = (remain < spaceInBlock) ? remain : spaceInBlock;

        memcpy(block + blockOff, buf + bytesWritten, toWrite);

        off_t blockAddr = useIndirect ? (*actualBlockOff) : (inode->blocks[(curOffset / BLOCK_SIZE)]);
        replicate_partial_block(blockAddr + blockOff, toWrite, block + blockOff);

        bytesWritten += toWrite;
    }

    inode->size += bytesWritten;
    replicate_inode(inode);

    return bytesWritten ? bytesWritten : -ENOSPC;
}


int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    int inodeNum = parsePath(path);
    if (inodeNum < 0) return -ENOENT;

    struct wfs_inode *inode = (struct wfs_inode *)(inodeStart + inodeNum*BLOCK_SIZE);
    if (!(inode->mode & S_IFDIR)) return -EBADF;

    // Update atime
    inode->atim = time(NULL);

    // Replicate inode changes in RAID 1
    if (disk_count > 1 && sb->raid_mode == 1) {
        replicate_inode(inode);
    }

    // Add current and parent directory entries
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // Iterate over directory entries
    int blockIter = 0;
    while (inode->blocks[blockIter] != 0 && blockIter < IND_BLOCK) {
        struct wfs_dentry *entries = (struct wfs_dentry *)(memStart + inode->blocks[blockIter]);
        int k = -1;
        while (entries->name[0] != 0 && ++k < BLOCK_SIZE / sizeof(struct wfs_dentry)) {
            struct wfs_inode *curr = (struct wfs_inode *)(inodeStart + entries->num * BLOCK_SIZE);
            struct stat stbuf;

            stbuf.st_uid = curr->uid;
            stbuf.st_gid = curr->gid;
            stbuf.st_mode = curr->mode;
            stbuf.st_nlink = curr->nlinks;
            stbuf.st_atim.tv_sec = curr->atim;
            stbuf.st_ctim.tv_sec = curr->ctim;
            stbuf.st_mtim.tv_sec = curr->mtim;
            stbuf.st_size = curr->size;
            stbuf.st_ino = curr->num;

            if (filler(buf, entries->name, &stbuf, 0)) {
                break;
            }
            entries++;
        }
        blockIter++;
    }
    return OK;
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod   = wfs_mknod,
    .mkdir   = wfs_mkdir,
    .unlink  = wfs_unlink,
    .rmdir   = wfs_rmdir,
    .read    = wfs_read,
    .write   = wfs_write,
    .readdir = wfs_readdir,
};

void usage(char *name) {
   printf("Usage: %s disk1 [disk2 ... diskN] [FUSE options] mount_point\n",name);
}

void free_resources() {
    // Unmap all disk maps and close file descriptors
    if (disk_maps) {
        for (int i = 0; i < disk_count; i++) {
            if (disk_maps[i]) {
                munmap(disk_maps[i], 0);  // 0 indicates freeing the entire mapping
            }
        }
        free(disk_maps);
    }

    if (fds) {
        for (int i = 0; i < disk_count; i++) {
            if (fds[i] >= 0) {
                close(fds[i]);
            }
        }
        free(fds);
    }
}

int main(int argc, char **argv) {
    signal(SIGUSR1, debugSignal);

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    // Allocate initial arrays dynamically
    fds = malloc(argc * sizeof(int));
    if (!fds) {
        perror("malloc fds");
        return 1;
    }
    disk_maps = malloc(argc * sizeof(char *));
    if (!disk_maps) {
        perror("malloc disk_maps");
        free(fds);
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        fds[i] = -1;  // Initialize file descriptors
        disk_maps[i] = NULL;
    }

    int argIndex = 1;
    while (argIndex < argc && argv[argIndex][0] != '-') {
        struct stat st;
        if (stat(argv[argIndex], &st) == 0) {
            fds[disk_count] = open(argv[argIndex], O_RDWR);
            if (fds[disk_count] < 0) {
                perror("open");
                free_resources();
                return 1;
            }
            disk_count++;
            argIndex++;
        } else {
            break;
        }
    }

    sb = mmap(NULL, sizeof(struct wfs_sb), PROT_WRITE | PROT_READ, MAP_SHARED, fds[0], 0);
    if (sb == MAP_FAILED) {
        perror("mmap");
        free_resources();
        return 1;
    }

    if (sb->disk_count != disk_count) {
        fprintf(stderr, "Error: number of disks does not match filesystem metadata. Expected %d got %d\n", (int)sb->disk_count, disk_count);
        free_resources();
        return -1;
    }

    int size = sizeof(struct wfs_sb) + sb->num_inodes / 8 + sb->num_data_blocks / 8
        + BLOCK_SIZE * sb->num_inodes
        + BLOCK_SIZE * sb->num_data_blocks;

    if (munmap(sb, sizeof(struct wfs_sb)) < 0) {
        perror("munmap");
    }

    for (int i = 0; i < disk_count; i++) {
        disk_maps[i] = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fds[i], 0);
        if (disk_maps[i] == MAP_FAILED) {
            perror("mmap");
            free_resources();
            return 1;
        }
    }

    memStart = disk_maps[0];
    sb = (struct wfs_sb *)memStart;
    iCount = sb->num_inodes;
    inodeMap = memStart + sb->i_bitmap_ptr;
    inodeStart = memStart + sb->i_blocks_ptr;
    dCount = sb->num_data_blocks;
    dataMap = memStart + sb->d_bitmap_ptr;
    dataStart = memStart + sb->d_blocks_ptr;

    int result = fuse_main(argc - disk_count, argv + disk_count, &ops, NULL);
    free_resources();  // Clean up before exiting
    return result;
}
