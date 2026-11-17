#ifndef USB_MSC_H_
#define USB_MSC_H_

#include "usb_desc.h"

#ifdef MSC_INTERFACE

#ifdef __cplusplus
extern "C" {
#endif

void usb_msc_bulk_only_reset(void);
void usb_msc_update(void);
void usb_msc_set_media_ready(uint8_t ready);

#ifdef __cplusplus
}
#endif

#endif // MSC_INTERFACE
#endif // USB_MSC_H_
