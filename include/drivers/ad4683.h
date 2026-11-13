#pragma once

#include <stdint.h>
#include "platform/spi.h"

typedef struct {
    int16_t a;
    int16_t b;
} ad4683_sample_t;

spi_status_t ad4683_init(void);
spi_status_t ad4683_read_sample(ad4683_sample_t *sample);
spi_status_t ad4683_set_external_reference(void);

