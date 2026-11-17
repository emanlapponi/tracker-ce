#ifndef USBaudio_h_
#define USBaudio_h_

#include "usb_desc.h"
#ifdef AUDIO_INTERFACE

#include <stdint.h>

#define FEATURE_MAX_VOLUME 0xFFF

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t usb_audio_receive_buffer[];
extern uint16_t usb_audio_transmit_buffer[];
extern void usb_audio_receive_callback(unsigned int len);
extern unsigned int usb_audio_transmit_callback(void);
int usb_audio_set_feature(void *stp, uint8_t *buf);
int usb_audio_get_feature(void *stp, uint8_t *data, uint32_t *datalen);

extern uint32_t usb_audio_sync_feedback;
extern uint8_t usb_audio_receive_setting;
extern uint8_t usb_audio_transmit_setting;

// Tracker AudioEngine bridge API
void usb_audio_reset_buffers(void);
void usb_audio_reset_tx(void);
const void *usb_audio_pull_rx_block(uint32_t *dst_interleaved, unsigned int frames);
void usb_audio_push_tx_block(const int16_t *left, const int16_t *right, unsigned int frames);
uint8_t usb_audio_is_tx_active(void);
uint8_t usb_audio_is_rx_active(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_INTERFACE
#endif // USBaudio_h_
