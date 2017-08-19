#ifndef PCA9548__H
#define PCA9548__H

#define NUM_BUSSES 8
#define PCA9548_CONTROL_ADDR 0x74

typedef struct {
    I2CSlave i2c;
    I2CBus *busses[NUM_BUSSES];

    /*state */
    uint8_t control_reg;
    enum i2c_event event;
    bool control_decoded;

    uint8_t chip_enable; /*property */
} PCA9548State;

#define TYPE_PCA9548 "pca9548"

#define PCA9548(obj) \
     OBJECT_CHECK(PCA9548State, (obj), TYPE_PCA9548)

#endif
