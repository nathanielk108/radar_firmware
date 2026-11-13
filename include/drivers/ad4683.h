#pragma once

#include <stdint.h>
#include "platform/spi.h"

typedef struct {
    int16_t a;
    int16_t b;
} ad4683_sample_t;

spi_status_t ad4683_init(void);
spi_status_t ad4683_read_sample(ad4683_sample_t *sample);


/* AD4683 16-bit command transaction; returns simultaneous previous results. */
spi_status_t ad4683_transfer(uint16_t command, uint16_t *a, uint16_t *b);
