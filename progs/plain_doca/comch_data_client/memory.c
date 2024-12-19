#include "comch_data_client.h"

struct cache_aligned_storage *create_cache_aligned_storage(
    uint32_t block_count,
    uint32_t block_size
) {
    struct cache_aligned_storage *storage = malloc(sizeof(struct cache_aligned_storage) + block_count * block_size + 64);

    storage->block_count = block_count;
    storage->block_size = block_size;
    storage->bytes = (uint8_t*)(((uintptr_t) &storage[1] + 64) / 64 * 64);

    return storage;
}

struct doca_mmap *open_memory_map(uint8_t *base, size_t size, struct doca_dev *dev, uint32_t permissions) {
    struct doca_mmap *map;
    doca_error_t err;

    err = doca_mmap_create(&map);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create memory map: %s", doca_error_get_descr(err));
        return NULL;
    }

    err = doca_mmap_set_memrange(map, base, size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set memory range: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_set_permissions(map, permissions);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set permissions: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_add_dev(map, dev);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not add device: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_start(map);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not start memory map: %s", doca_error_get_descr(err));
        goto failure;
    }

    return map;

failure:
    doca_mmap_destroy(map);
    return NULL;
}

struct doca_buf_inventory *open_buffer_inventory(uint32_t max_buffers) {
    doca_error_t err;
    struct doca_buf_inventory *inv;

    err = doca_buf_inventory_create(max_buffers, &inv);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create buffer inventory: %s", doca_error_get_descr(err));
        return NULL;
    }

    err = doca_buf_inventory_start(inv);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not start buffer inventory: %s", doca_error_get_descr(err));
        goto failure;
    }

    return inv;

failure:
    doca_buf_inventory_destroy(inv);
    return NULL;
}
