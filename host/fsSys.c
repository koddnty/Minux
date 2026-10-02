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

/* 新建/清空镜像：替代 dd if=/dev/zero of=xxx bs=1M count=128。
 * ftruncate 出来的空洞读出来就是 0，所以不必真的写 128MB 个字节。 */
int miDiskCreate(const char* path, uint64_t bytes) {
    if (g_disk_fd != -1) {
        close(g_disk_fd);
        g_disk_fd = -1;
    }

    g_disk_fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);

    if (g_disk_fd == -1) {
        return -1;
    }

    if (bytes > 0 && ftruncate(g_disk_fd, (off_t)bytes) != 0) {
        close(g_disk_fd);
        g_disk_fd = -1;
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

