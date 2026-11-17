#include "usb_audio.h"
#include "usb_dev.h"
#include "kinetis.h"
#include <string.h>

#if defined(AUDIO_INTERFACE) && F_CPU >= 20000000

#define DMABUFATTR __attribute__((section(".dmabuffers"), aligned(4)))
#define USB_AUDIO_RING_FRAMES 1024U // power of 2 (~23.2 ms @ 44.1 kHz)
#define USB_AUDIO_RING_MASK   (USB_AUDIO_RING_FRAMES - 1U)

uint16_t usb_audio_receive_buffer[AUDIO_RX_SIZE / 2] DMABUFATTR;
uint16_t usb_audio_transmit_buffer[AUDIO_TX_SIZE / 2] DMABUFATTR;
uint32_t usb_audio_sync_feedback DMABUFATTR;
uint8_t usb_audio_receive_setting = 0;
uint8_t usb_audio_transmit_setting = 0;

static uint32_t rx_ring[USB_AUDIO_RING_FRAMES] __attribute__((section(".noinit.$EXTERNAL_RAM"), aligned(4)));
static volatile uint32_t rx_write_idx = 0;
static volatile uint32_t rx_read_idx = 0;
static volatile uint8_t rx_primed = 0;

static uint32_t tx_ring[USB_AUDIO_RING_FRAMES] __attribute__((section(".noinit.$EXTERNAL_RAM"), aligned(4)));
static volatile uint32_t tx_write_idx = 0;
static volatile uint32_t tx_read_idx = 0;
static volatile uint8_t tx_primed = 0;

// Nominal 44.1176 kHz in 10.14 fixed point: (44.117647 * 16384) << 8
static uint32_t feedback_accumulator = (722823U << 8);
static int16_t feature_mute = 0;
static int16_t feature_volume = FEATURE_MAX_VOLUME / 2;

void usb_audio_reset_tx(void)
{
	tx_write_idx = 0;
	tx_read_idx = 0;
	tx_primed = 0;
}

void usb_audio_reset_buffers(void)
{
	__disable_irq();
	rx_write_idx = 0;
	rx_read_idx = 0;
	rx_primed = 0;
	tx_write_idx = 0;
	tx_read_idx = 0;
	tx_primed = 0;
	feedback_accumulator = (722823U << 8);
	usb_audio_sync_feedback = feedback_accumulator >> 8;
	__enable_irq();
}

uint8_t usb_audio_is_tx_active(void)
{
	return (usb_configuration && usb_audio_tx_interface != 0xFF && usb_audio_transmit_setting > 0) ? 1 : 0;
}

uint8_t usb_audio_is_rx_active(void)
{
	return (usb_configuration && usb_audio_rx_interface != 0xFF && usb_audio_receive_setting > 0) ? 1 : 0;
}

// Called from USBOTG ISR (priority 7) when an isochronous OUT packet arrives
void usb_audio_receive_callback(unsigned int len)
{
	const unsigned int frames = len >> 2; // 4 bytes per stereo 16-bit frame
	if (frames == 0 || usb_audio_receive_setting == 0) {
		return;
	}

	const uint32_t *src = (const uint32_t *)usb_audio_receive_buffer;
	uint32_t w = rx_write_idx;
	uint32_t r = rx_read_idx;
	uint32_t free_frames = USB_AUDIO_RING_FRAMES - (w - r);

	unsigned int to_write = frames < free_frames ? frames : free_frames;
	for (unsigned int i = 0; i < to_write; i++) {
		rx_ring[w & USB_AUDIO_RING_MASK] = src[i];
		w++;
	}
	rx_write_idx = w;
}

// Called from AudioInput I2S DMA ISR (priority 4) every 128 frames
const void *usb_audio_pull_rx_block(uint32_t *dst_interleaved, unsigned int frames)
{
	if (!usb_audio_is_rx_active()) {
		if (rx_primed || rx_write_idx != rx_read_idx) {
			rx_write_idx = 0;
			rx_read_idx = 0;
			rx_primed = 0;
		}
		return NULL;
	}

	const uint32_t w = rx_write_idx;
	uint32_t r = rx_read_idx;
	const uint32_t avail = w - r;

	// Update 10.14 async feedback based on target fill (~256 frames)
	const int32_t err = 256 - (int32_t)avail;
	feedback_accumulator += err / 2;
	int32_t fb = (int32_t)(feedback_accumulator >> 8) + err * 64;
	if (fb < 700000) fb = 700000;
	if (fb > 745000) fb = 745000;
	usb_audio_sync_feedback = (uint32_t)fb;

	if (!rx_primed) {
		if (avail < 256) {
			return NULL;
		}
		rx_primed = 1;
	}

	if (avail < frames) {
		rx_primed = 0;
		return NULL;
	}

	for (unsigned int i = 0; i < frames; i++) {
		dst_interleaved[i] = rx_ring[r & USB_AUDIO_RING_MASK];
		r++;
	}
	rx_read_idx = r;
	return dst_interleaved;
}

// Called from AudioStream::update_all() (AudioOutput DMA ISR, priority 5)
void usb_audio_push_tx_block(const int16_t *left, const int16_t *right, unsigned int frames)
{
	if (!usb_audio_is_tx_active()) {
		return;
	}

	uint32_t w = tx_write_idx;
	__disable_irq();
	const uint32_t r = tx_read_idx;
	const uint32_t used = w - r;

	// If ring overflowed, advance read pointer to keep latest audio
	if (used + frames > USB_AUDIO_RING_FRAMES) {
		tx_read_idx = w + frames - USB_AUDIO_RING_FRAMES;
	}
	__enable_irq();

	for (unsigned int i = 0; i < frames; i++) {
		const uint32_t sample = ((uint32_t)(uint16_t)right[i] << 16) | (uint16_t)left[i];
		tx_ring[w & USB_AUDIO_RING_MASK] = sample;
		w++;
	}
	tx_write_idx = w;
}

// Called from USBOTG ISR (priority 7) every ~1 ms to fill isochronous IN packet
unsigned int usb_audio_transmit_callback(void)
{
	// 16.16 fixed-point phase accumulator for nominal 44.117647 frames/ms
	static uint32_t tx_phase = 0;
	static int32_t avg_avail = 512 << 4;

	const uint32_t w = tx_write_idx;
	uint32_t r = tx_read_idx;
	const uint32_t avail = w - r;

	if (!tx_primed) {
		if (avail < 512) {
			memset(usb_audio_transmit_buffer, 0, 44 * 4);
			return 44 * 4;
		}
		tx_primed = 1;
		avg_avail = (int32_t)avail << 4;
		tx_phase = 0;
	}

	// Smooth fill-level tracking (tau ~ 16 ms) to avoid reacting to 128-frame bursts
	avg_avail += ((int32_t)avail - (avg_avail >> 4));
	const int32_t smooth_avail = avg_avail >> 4;

	// Nominal 44.117647 frames/ms in 0.16 fixed point = 7710 (0.117647 * 65536)
	int32_t frac_step = 7710;
	if (smooth_avail > 640) {
		frac_step += 1500; // gentle +0.023 frames/ms
	} else if (smooth_avail < 384) {
		frac_step -= 1500; // gentle -0.023 frames/ms
	}

	tx_phase += (uint32_t)frac_step;
	const uint32_t target = 44U + (tx_phase >> 16);
	tx_phase &= 0xFFFFU;

	uint32_t *dst = (uint32_t *)usb_audio_transmit_buffer;
	if (avail < target) {
		memset(dst, 0, target * 4);
		return target * 4;
	}

	for (unsigned int i = 0; i < target; i++) {
		dst[i] = tx_ring[r & USB_AUDIO_RING_MASK];
		r++;
	}
	tx_read_idx = r;
	return target * 4;
}

struct setup_struct {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint8_t bChannel;
	uint8_t bCS;
	uint8_t bIfEp;
	uint8_t bEntityId;
	uint16_t wLength;
};

int usb_audio_get_feature(void *stp, uint8_t *data, uint32_t *datalen)
{
	struct setup_struct setup = *((struct setup_struct *)stp);
	if (setup.bmRequestType == 0xA1) {
		if (setup.bCS == 0x01) { // mute
			data[0] = (uint8_t)feature_mute;
			*datalen = 1;
			return 1;
		} else if (setup.bCS == 0x02) { // volume
			if (setup.bRequest == 0x81) { // GET_CUR
				data[0] = feature_volume & 0xFF;
				data[1] = (feature_volume >> 8) & 0xFF;
			} else if (setup.bRequest == 0x82) { // GET_MIN
				data[0] = 0;
				data[1] = 0;
			} else if (setup.bRequest == 0x83) { // GET_MAX
				data[0] = FEATURE_MAX_VOLUME & 0xFF;
				data[1] = (FEATURE_MAX_VOLUME >> 8) & 0x0F;
			} else if (setup.bRequest == 0x84) { // GET_RES
				data[0] = 1;
				data[1] = 0;
			} else {
				return 0;
			}
			*datalen = 2;
			return 1;
		}
	}
	return 0;
}

int usb_audio_set_feature(void *stp, uint8_t *buf)
{
	struct setup_struct setup = *((struct setup_struct *)stp);
	if (setup.bmRequestType == 0x21) {
		if (setup.bCS == 0x01 && setup.bRequest == 0x01) {
			feature_mute = buf[0];
			return 1;
		} else if (setup.bCS == 0x02 && setup.bRequest == 0x01) {
			feature_volume = (int16_t)(buf[0] | (buf[1] << 8));
			return 1;
		}
	}
	return 0;
}

#endif // AUDIO_INTERFACE && F_CPU >= 20000000
