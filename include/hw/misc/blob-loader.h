#ifndef BLOB_LOADER_H
#define BLOB_LOADER_H

typedef struct BlobLoaderState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    uint64_t addr;
    uint64_t data;
    uint8_t data_len;
    char *file;

    CPUState *cpu;
    uint8_t cpu_nr;
    bool force_raw;
} BlobLoaderState;

#define TYPE_BLOB_LOADER "loader"
#define BLOB_LOADER(obj) OBJECT_CHECK(BlobLoaderState, (obj), TYPE_BLOB_LOADER)

#endif

