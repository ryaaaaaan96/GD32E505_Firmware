#ifndef FAL_H
#define FAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fal_flash_dev {
    char name[8];
    uint32_t addr;
    size_t len;
    size_t blk_size;
};

struct fal_partition {
    char name[8];
    char flash_name[8];
    const struct fal_flash_dev *flash_dev;
    uint32_t offset;
    uint32_t len;
};

void fal_init(void);
const struct fal_partition *fal_partition_find(const char *name);
const struct fal_flash_dev *fal_flash_device_find(const char *name);
int fal_partition_read(const struct fal_partition *part, uint32_t address,
                       uint8_t *buffer, size_t size);
int fal_partition_write(const struct fal_partition *part, uint32_t address,
                        const uint8_t *buffer, size_t size);
int fal_partition_erase(const struct fal_partition *part, uint32_t address,
                        size_t size);

#ifdef __cplusplus
}
#endif

#endif
