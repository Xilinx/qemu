#include "qemu-common.h"
#include "qemu/hexdump.h"

void hexdump(const char *buf, FILE *fp, const char *prefix, size_t size)
{
    unsigned int b;

    for (b = 0; b < size; b++) {
        if ((b % 16) == 0) {
            fprintf(fp, "%s: %04x:", prefix, b);
        }
        if ((b % 4) == 0) {
            fprintf(fp, " ");
        }
        fprintf(fp, " %02x", (unsigned char)buf[b]);
        if ((b % 16) == 15) {
            fprintf(fp, "\n");
        }
    }
    if ((b % 16) != 0) {
        fprintf(fp, "\n");
    }
}
