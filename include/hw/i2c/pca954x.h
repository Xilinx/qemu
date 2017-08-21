#ifndef PCA954X__H
#define PCA954X__H

#include "hw/i2c/i2c.h"
#define MAX_I2C_BUSES 8
#define PCA954X_CONTROL_ADDR 0x74

typedef struct pca954x_type {
    const char *name;
    uint8_t lanes;   /* Number of i2c out buses */
    bool mux;        /* mux or switch */
} pca954x_type;

typedef struct PCA954X_class {
    I2CSlaveClass parent_class;

    pca954x_type *device;
} PCA954XClass;

typedef struct {
    I2CSlave i2c;
    I2CBus *busses[MAX_I2C_BUSES];

    /*state */
    uint8_t control_reg;
    enum i2c_event event;
    bool control_decoded;
    uint8_t active_lanes;

    bool mux;
    uint8_t lanes;
    uint8_t chip_enable; /*property */
} PCA954XState;

#define TYPE_PCA954X "pca954x"

#define PCA954X(obj) \
     OBJECT_CHECK(PCA954XState, (obj), TYPE_PCA954X)
#define PCA954X_CLASS(klass) \
     OBJECT_CLASS_CHECK(PCA954XClass, klass, TYPE_PCA954X)
#define PCA954X_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PCA954XClass, (obj), TYPE_PCA954X)

#endif
