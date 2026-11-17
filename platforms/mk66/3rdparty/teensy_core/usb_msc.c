#include "usb_msc.h"
#include "usb_dev.h"
#include "ff.h"
#include "diskio.h"
#include "fsl_sd_disk.h"
#include <string.h>

#if defined(MSC_INTERFACE) && F_CPU >= 20000000

#define CBW_SIGNATURE 0x43425355U
#define CSW_SIGNATURE 0x53425355U

#define MSC_SECTOR_SIZE 512U
#define MSC_BUF_SECTORS 8U
#define MSC_BUF_BYTES   (MSC_SECTOR_SIZE * MSC_BUF_SECTORS)

typedef enum {
	MSC_STATE_IDLE = 0,
	MSC_STATE_DATA_IN,
	MSC_STATE_DATA_OUT,
	MSC_STATE_SEND_CSW,
} msc_state_t;

static msc_state_t msc_state = MSC_STATE_IDLE;
static uint8_t msc_media_ready = 0;

static uint32_t cbw_tag = 0;
static uint32_t cbw_data_len = 0;   // bytes of the data phase still to transfer (the residue)
static uint32_t cbw_total_len = 0;  // dCBWDataTransferLength
static uint8_t  cbw_dir_in = 0;     // the host expects data from the device
static uint8_t  cbw_zlp_sent = 0;
static uint8_t  cbw_status = 0;

static uint32_t cur_lba = 0;
static uint32_t blocks_remaining = 0;
static uint32_t chunk_blocks = 0;
static uint32_t buf_offset = 0;
static uint32_t buf_valid = 0;

// Sense data: No Sense
static uint8_t sense_key = 0x00;
static uint8_t sense_asc = 0x00;
static uint8_t sense_ascq = 0x00;

static uint8_t msc_sector_buf[MSC_BUF_BYTES] __attribute__((aligned(4)));

static inline uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline uint16_t read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static inline void write_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void set_sense(uint8_t key, uint8_t asc, uint8_t ascq)
{
	sense_key = key;
	sense_asc = asc;
	sense_ascq = ascq;
}

static int send_bytes(const uint8_t *src, uint32_t len)
{
	uint32_t sent = 0;
	while (sent < len) {
		usb_packet_t *pkt = usb_malloc();
		if (!pkt) {
			return 0;
		}
		uint32_t n = len - sent;
		if (n > 64) n = 64;
		memcpy(pkt->buf, src + sent, n);
		pkt->len = (uint16_t)n;
		usb_tx(MSC_TX_ENDPOINT, pkt);
		sent += n;
	}
	return 1;
}

static int send_zlp(void)
{
	usb_packet_t *pkt = usb_malloc();
	if (!pkt) {
		return 0;
	}
	pkt->len = 0;
	usb_tx(MSC_TX_ENDPOINT, pkt);
	return 1;
}

static void send_csw(void)
{
	// Bulk-Only Transport: when the host expects more data than the device
	// sent (a failed or unsupported command, or a short reply), the data
	// phase must end before the CSW. Without this the host takes the CSW as
	// data, waits for a CSW that never comes and only recovers after a
	// timeout: Windows took 60 s to start the drive. A short packet already
	// ends it; otherwise end it with a zero-length packet.
	if (cbw_dir_in && cbw_data_len > 0 && !cbw_zlp_sent &&
	    ((cbw_total_len - cbw_data_len) % 64) == 0) {
		if (!send_zlp()) {
			msc_state = MSC_STATE_SEND_CSW;
			return;
		}
		cbw_zlp_sent = 1;
	}

	uint8_t csw[13];
	write_le32(&csw[0], CSW_SIGNATURE);
	write_le32(&csw[4], cbw_tag);
	write_le32(&csw[8], cbw_data_len);
	csw[12] = cbw_status;
	if (send_bytes(csw, sizeof(csw))) {
		msc_state = MSC_STATE_IDLE;
	} else {
		msc_state = MSC_STATE_SEND_CSW;
	}
}

void usb_msc_set_media_ready(uint8_t ready)
{
	msc_media_ready = ready;
}

void usb_msc_bulk_only_reset(void)
{
	msc_state = MSC_STATE_IDLE;
	cbw_data_len = 0;
	cbw_status = 0;
	blocks_remaining = 0;
	chunk_blocks = 0;
	buf_offset = 0;
	buf_valid = 0;
}

static void handle_cbw(const uint8_t *cbw)
{
	if (read_le32(&cbw[0]) != CBW_SIGNATURE) {
		return;
	}

	cbw_tag = read_le32(&cbw[4]);
	cbw_data_len = read_le32(&cbw[8]);
	cbw_total_len = cbw_data_len;
	cbw_dir_in = (cbw[12] & 0x80) != 0;
	cbw_zlp_sent = 0;
	const uint8_t *cdb = &cbw[15];
	const uint8_t opcode = cdb[0];

	cbw_status = 0; // Pass

	switch (opcode) {
		case 0x00: // TEST UNIT READY
			if (!msc_media_ready) {
				set_sense(0x02, 0x3A, 0x00); // Not Ready, Medium Not Present
				cbw_status = 1;
			}
			send_csw();
			break;

		case 0x12: { // INQUIRY
			static const uint8_t inquiry[36] = {
				0x00, // Direct access block device
				0x80, // Removable media
				0x04, // SPC-2
				0x02, // Response data format
				31,   // Additional length
				0x00, 0x00, 0x00,
				'P','o','l','y','e','n','d',' ',
				'T','r','a','c','k','e','r',' ','S','D',' ',' ',' ',' ',' ',' ',
				'1','.','0','0'
			};
			uint32_t len = cdb[4];
			if (len > sizeof(inquiry)) len = sizeof(inquiry);
			if (len > cbw_data_len) len = cbw_data_len;
			if (len > 0) {
				send_bytes(inquiry, len);
				cbw_data_len -= len;
			}
			send_csw();
			break;
		}

		case 0x03: { // REQUEST SENSE
			uint8_t sense[18];
			memset(sense, 0, sizeof(sense));
			sense[0] = 0x70;
			sense[2] = sense_key;
			sense[7] = 10;
			sense[12] = sense_asc;
			sense[13] = sense_ascq;
			set_sense(0x00, 0x00, 0x00);
			uint32_t len = cdb[4];
			if (len > sizeof(sense)) len = sizeof(sense);
			if (len > cbw_data_len) len = cbw_data_len;
			if (len > 0) {
				send_bytes(sense, len);
				cbw_data_len -= len;
			}
			send_csw();
			break;
		}

		case 0x25: { // READ CAPACITY (10)
			if (!msc_media_ready) {
				set_sense(0x02, 0x3A, 0x00);
				cbw_status = 1;
				send_csw();
				break;
			}
			uint32_t sector_count = 0;
			uint32_t sector_size = MSC_SECTOR_SIZE;
			sd_disk_ioctl(SDDISK, GET_SECTOR_COUNT, &sector_count);
			sd_disk_ioctl(SDDISK, GET_SECTOR_SIZE, &sector_size);
			uint8_t cap[8];
			write_be32(&cap[0], sector_count > 0 ? sector_count - 1 : 0);
			write_be32(&cap[4], sector_size);
			uint32_t len = cbw_data_len < 8 ? cbw_data_len : 8;
			if (len > 0) {
				send_bytes(cap, len);
				cbw_data_len -= len;
			}
			send_csw();
			break;
		}

		case 0x1A: { // MODE SENSE (6)
			uint8_t mode[4] = { 0x03, 0x00, 0x00, 0x00 };
			uint32_t len = cbw_data_len < 4 ? cbw_data_len : 4;
			if (len > 0) {
				send_bytes(mode, len);
				cbw_data_len -= len;
			}
			send_csw();
			break;
		}

		case 0x23: { // READ FORMAT CAPACITIES (Windows asks removable drives for it)
			if (!msc_media_ready) {
				set_sense(0x02, 0x3A, 0x00);
				cbw_status = 1;
				send_csw();
				break;
			}
			uint32_t sector_count = 0;
			sd_disk_ioctl(SDDISK, GET_SECTOR_COUNT, &sector_count);
			uint8_t caps[12];
			memset(caps, 0, sizeof(caps));
			caps[3] = 8;                        // capacity list length
			write_be32(&caps[4], sector_count); // number of blocks
			caps[8] = 0x02;                     // formatted media
			caps[9] = (uint8_t)(MSC_SECTOR_SIZE >> 16);
			caps[10] = (uint8_t)(MSC_SECTOR_SIZE >> 8);
			caps[11] = (uint8_t)MSC_SECTOR_SIZE;
			uint32_t len = cbw_data_len < sizeof(caps) ? cbw_data_len : sizeof(caps);
			if (len > 0) {
				send_bytes(caps, len);
				cbw_data_len -= len;
			}
			send_csw();
			break;
		}

		case 0x5A: { // MODE SENSE (10): header only, not write-protected
			uint8_t mode[8] = { 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			uint32_t len = cbw_data_len < sizeof(mode) ? cbw_data_len : sizeof(mode);
			if (len > 0) {
				send_bytes(mode, len);
				cbw_data_len -= len;
			}
			send_csw();
			break;
		}

		case 0x1E: // PREVENT ALLOW MEDIUM REMOVAL
		case 0x1B: // START STOP UNIT
		case 0x35: // SYNCHRONIZE CACHE (10)
			send_csw();
			break;

		case 0x28: // READ (10)
			if (!msc_media_ready) {
				set_sense(0x02, 0x3A, 0x00);
				cbw_status = 1;
				send_csw();
				break;
			}
			cur_lba = read_be32(&cdb[2]);
			blocks_remaining = read_be16(&cdb[7]);
			buf_offset = 0;
			buf_valid = 0;
			if (blocks_remaining == 0) {
				send_csw();
			} else {
				msc_state = MSC_STATE_DATA_IN;
			}
			break;

		case 0x2A: // WRITE (10)
			if (!msc_media_ready) {
				set_sense(0x02, 0x3A, 0x00);
				cbw_status = 1;
				send_csw();
				break;
			}
			cur_lba = read_be32(&cdb[2]);
			blocks_remaining = read_be16(&cdb[7]);
			chunk_blocks = blocks_remaining < MSC_BUF_SECTORS ? blocks_remaining : MSC_BUF_SECTORS;
			buf_offset = 0;
			buf_valid = chunk_blocks * MSC_SECTOR_SIZE;
			if (blocks_remaining == 0) {
				send_csw();
			} else {
				msc_state = MSC_STATE_DATA_OUT;
			}
			break;

		default:
			set_sense(0x05, 0x20, 0x00); // Illegal Request, Invalid Command
			cbw_status = 1;
			send_csw();
			break;
	}
}

void usb_msc_update(void)
{
	if (usb_msc_interface == 0xFF || !usb_configuration) {
		return;
	}

	if (msc_state == MSC_STATE_SEND_CSW) {
		send_csw();
		return;
	}

	if (msc_state == MSC_STATE_IDLE) {
		usb_packet_t *pkt = usb_rx(MSC_RX_ENDPOINT);
		if (pkt) {
			if (pkt->len == 31) {
				handle_cbw(pkt->buf);
			}
			usb_free(pkt);
		}
	}

	if (msc_state == MSC_STATE_DATA_IN) {
		while (blocks_remaining > 0 || buf_offset < buf_valid) {
			if (buf_offset >= buf_valid) {
				uint32_t nblocks = blocks_remaining < MSC_BUF_SECTORS ? blocks_remaining : MSC_BUF_SECTORS;
				if (sd_disk_read(SDDISK, msc_sector_buf, cur_lba, (uint8_t)nblocks) != RES_OK) {
					set_sense(0x03, 0x11, 0x00); // Medium Error, Unrecovered Read Error
					cbw_status = 1;
					blocks_remaining = 0;
					buf_offset = 0;
					buf_valid = 0;
					break;
				}
				cur_lba += nblocks;
				blocks_remaining -= nblocks;
				buf_offset = 0;
				buf_valid = nblocks * MSC_SECTOR_SIZE;
			}

			while (buf_offset < buf_valid) {
				if (usb_tx_packet_count(MSC_TX_ENDPOINT) >= 4) {
					return; // let USB drain TX queue
				}
				usb_packet_t *pkt = usb_malloc();
				if (!pkt) {
					return;
				}
				uint32_t n = buf_valid - buf_offset;
				if (n > 64) n = 64;
				memcpy(pkt->buf, msc_sector_buf + buf_offset, n);
				pkt->len = (uint16_t)n;
				usb_tx(MSC_TX_ENDPOINT, pkt);
				buf_offset += n;
				cbw_data_len -= n;
			}
		}
		send_csw();
	}

	if (msc_state == MSC_STATE_DATA_OUT) {
		while (blocks_remaining > 0) {
			usb_packet_t *pkt = usb_rx(MSC_RX_ENDPOINT);
			if (!pkt) {
				return;
			}
			uint32_t copy_len = pkt->len;
			if (copy_len > buf_valid - buf_offset) {
				copy_len = buf_valid - buf_offset;
			}
			memcpy(msc_sector_buf + buf_offset, pkt->buf, copy_len);
			usb_free(pkt);
			buf_offset += copy_len;
			cbw_data_len -= copy_len;

			if (buf_offset >= buf_valid) {
				if (sd_disk_write(SDDISK, msc_sector_buf, cur_lba, (uint8_t)chunk_blocks) != RES_OK) {
					set_sense(0x03, 0x0C, 0x00); // Medium Error, Write Error
					cbw_status = 1;
					blocks_remaining = 0;
					break;
				}
				cur_lba += chunk_blocks;
				blocks_remaining -= chunk_blocks;
				chunk_blocks = blocks_remaining < MSC_BUF_SECTORS ? blocks_remaining : MSC_BUF_SECTORS;
				buf_offset = 0;
				buf_valid = chunk_blocks * MSC_SECTOR_SIZE;
			}
		}
		send_csw();
	}
}

#endif // MSC_INTERFACE
