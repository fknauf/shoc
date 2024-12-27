#include "dma_client.h"

struct cache_aligned_data *create_cache_aligned_buffer(
    uint32_t block_count,
    uint32_t block_size
) {
    struct cache_aligned_data *data = malloc(sizeof(struct cache_aligned_data) + block_count * block_size + 64);

    if(data != NULL) {
        data->base_ptr = (uint8_t *)(((uintptr_t) &data[1] + 64) / 64 * 64);
        data->block_count = block_count;
        data->block_size = block_size;
    }

    return data;
}

struct doca_pe *open_progress_engine(int epoll_fd) {
    struct doca_pe *engine;
    doca_error_t err;

    err = doca_pe_create(&engine);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create progress engine: %s", doca_error_get_descr(err));
        return NULL;
    }

    doca_event_handle_t event_handle;
    err = doca_pe_get_notification_handle(engine, &event_handle);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not obtain notification handle: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in) != 0) {
        LOG_ERROR("could not attach to epoll handle: %s", strerror(errno));
        goto failure;
    }

    return engine;

failure:
    doca_pe_destroy(engine);
    return NULL;
}

struct doca_dev *open_device(char const *pci_addr) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    doca_error_t err;

    err = doca_devinfo_create_list(&dev_list, &nb_devs);

    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get device list: %s", doca_error_get_descr(err));
        return NULL;
    }

    for(uint32_t i = 0; i < nb_devs; ++i) {
        uint8_t is_addr_equal = 0;

        err = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("could not check device pci address: %s", doca_error_get_descr(err));
            continue;
        }

        if(is_addr_equal
            && doca_comch_cap_client_is_supported(dev_list[i]) == DOCA_SUCCESS
            && doca_dma_cap_task_memcpy_is_supported(dev_list[i]) == DOCA_SUCCESS
        ) {
            err = doca_dev_open(dev_list[i], &result);

            if(err == DOCA_SUCCESS) {
                goto cleanup;
            } else {
                LOG_ERROR("could not open device: %s", doca_error_get_descr(err));
            }
        }
    }

    LOG_ERROR("no comch server device found");

cleanup:
    doca_devinfo_destroy_list(dev_list);
    return result;
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
