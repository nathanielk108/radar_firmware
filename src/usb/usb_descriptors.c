#include "tusb.h"
#include <string.h>

#define USB_VID 0xCAFE
#define USB_PID 0x4000
#define USB_BCD 0x0200

/* ------------------------------------------------------------------
 * Device descriptor
 * ------------------------------------------------------------------ */

static tusb_desc_device_t const device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,

    .bNumConfigurations = 1
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&device_descriptor;
}

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static uint8_t const configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        ITF_NUM_TOTAL,
        0,
        CONFIG_TOTAL_LEN,
        0x00,
        100
    ),

    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC,
        4,
        EPNUM_CDC_NOTIF,
        16,
        EPNUM_CDC_OUT,
        EPNUM_CDC_IN,
        64
    )
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC
};

static char const *string_desc[] = {
    (const char[]){ 0x09, 0x04 },
    "Capstone",
    "Handheld FMCW Radar",
    "0001",
    "Debug Console"
};

static uint16_t desc_string[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    size_t count;

    if (index == 0) {
        desc_string[1] = 0x0409;
        count = 1;
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) {
            return NULL;
        }

        const char *str = string_desc[index];

        count = strlen(str);

        if (count > 31) {
            count = 31;
        }

        for (size_t i = 0; i < count; ++i) {
            desc_string[1 + i] = str[i];
        }
    }

    desc_string[0] =
        (uint16_t)((TUSB_DESC_STRING << 8) |
                   (2 * count + 2));

    return desc_string;
}
