
#include "copyFs.h"

int miCopyFile(
const char* local_path,
const char* disk_path,
uint32_t sector,
const char* name
) {
    FILE* file = fopen(local_path, "rb");

    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    long file_size = ftell(file);

    if (file_size < 0 || file_size > UINT32_MAX) {
        fclose(file);
        return -1;
    }

    rewind(file);

    uint32_t size = (uint32_t)file_size;
    char* buffer = NULL;

    if (size > 0) {
        buffer = malloc(size);

        if (buffer == NULL) {
            fclose(file);
            return -1;
        }

        if (fread(buffer, 1, size, file) != size) {
            free(buffer);
            fclose(file);
            return -1;
        }
    }

    fclose(file);

    int ret = miCoreWriteFile(
        (char*)disk_path,
        (char*)name,
        buffer,
        &size,
        sector
    );

    free(buffer);

    return ret;
}
