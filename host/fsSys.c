#include "fsSys.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static int g_disk_fd = -1;

int miDiskOpen(const char* path) {
    if (g_disk_fd != -1) {
        return -1;
    }

    g_disk_fd = open(path, O_RDWR);

    if (g_disk_fd == -1) {
        return -1;
    }

    return 0;
}

void miDiskClose(void) {
    if (g_disk_fd != -1) {
        close(g_disk_fd);
        g_disk_fd = -1;
    }
}

int miReadSector(uint32_t lba, void* buffer, uint32_t count) {
    if (g_disk_fd == -1 || buffer == NULL || count == 0) {
        return -1;
    }

    off_t offset = (off_t)lba * MI_SECTOR_SIZE;
    size_t size = (size_t)count * MI_SECTOR_SIZE;

    ssize_t ret = pread(g_disk_fd, buffer, size, offset);

    if (ret != (ssize_t)size) {
        return -1;
    }

    return 0;
}

int miWriteSector(uint32_t lba, const void* buffer, uint32_t count) {
    if (g_disk_fd == -1 || buffer == NULL || count == 0) {
        return -1;
    }

    off_t offset = (off_t)lba * MI_SECTOR_SIZE;
    size_t size = (size_t)count * MI_SECTOR_SIZE;

    ssize_t ret = pwrite(g_disk_fd, buffer, size, offset);

    if (ret != (ssize_t)size) {
        return -1;
    }

    return 0;
}

