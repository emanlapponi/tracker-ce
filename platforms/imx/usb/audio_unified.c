/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016,2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
/*${standard_header_anchor}*/
#include "log.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_config.h"

#include "usb_device_audio.h"
#include "usb_device_class.h"

#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"

#include "composite.h"

#include "board.h"
#include "clock_config.h"
#include "cr_section_macros.h"
#include "fsl_debug_console.h"
#include "fsl_device_registers.h"

#define SAMPLES_CODEC       128
#define CODEC_SAMPLE_RATE   44098
#define CHANNELS_REC_COUNT  AUDIO_IN_FORMAT_CHANNELS
#define CHANNELS_PLAY_COUNT AUDIO_OUT_FORMAT_CHANNELS
#define USB_SAMPLE_RATE     AUDIO_IN_SAMPLING_RATE_KHZ
#define SINE_FREQ           441
#define SINE_SAMPLES_COUNT  (CHANNELS_PLAY_COUNT * (USB_SAMPLE_RATE / SINE_FREQ))
#define SAMPLES_REC_COUNT   (CHANNELS_REC_COUNT * SAMPLES_CODEC)
#define SAMPLES_PLAY_COUNT  (CHANNELS_PLAY_COUNT * SAMPLES_CODEC)

#if (HS_ISO_IN_ENDP_INTERVAL == 4)
#define USB_FRAMES_PER_MS (1)
#elif (HS_ISO_IN_ENDP_INTERVAL == 3)
#define USB_FRAMES_PER_MS (2)
#elif (HS_ISO_IN_ENDP_INTERVAL == 2)
#define USB_FRAMES_PER_MS (4)
#elif (HS_ISO_IN_ENDP_INTERVAL == 1)
#define USB_FRAMES_PER_MS (8)
#else
#error "For USB AUDIO HS_ISO_IN_ENDP_INTERVAL must not be greater than 4"
#endif
#define USB_FRAMES_PER_SEC (1000 * USB_FRAMES_PER_MS)
#define SAMPLES_PART_EVEN  ((USB_SAMPLE_RATE / USB_FRAMES_PER_SEC) + 1)
#define SAMPLES_PART_ODD   (USB_SAMPLE_RATE / USB_FRAMES_PER_SEC)
#define AUDIO_BUFFERS      3

#if ((CHANNELS_COUNT * SAMPLES_PART_EVEN * AUDIO_IN_FORMAT_SIZE) > HS_ISO_IN_ENDP_PACKET_SIZE)
#error "HS_ISO_IN_ENDP_PACKET_SIZE too small or one must adapt HS_ISO_IN_ENDP_INTERVAL"
#endif

#define USB_LOG_ENABLED 0

__NOINIT(EXTERNAL_RAM) int16_t audioRecBuff[SAMPLES_REC_COUNT * AUDIO_BUFFERS];
__BSS(SRAM_OC) int16_t audioPlayBuff[SAMPLES_PLAY_COUNT * AUDIO_BUFFERS];

volatile unsigned int audioRecWriteOffset = 0;
volatile unsigned int audioPlayReadOffset = 0;
volatile uint32_t audioToSend = 0;
volatile int32_t samplesToRecv = 0;
unsigned int samplesToSend = 0;

int currSampleRate = CODEC_SAMPLE_RATE;
float expectedFrameRate = 0;
float currFrameRate = 0;
unsigned int currFrameCount = 0;
unsigned int currSampleCount = 0;

bool usbIsoReady = false;
bool audioReady = false;
bool audioPlayReady = false;

uint32_t minToSend = UINT32_MAX;
uint32_t maxToSend = 0;

signed int currError = 0;
signed int prevError = 0;

int delta = 0;
int sent = 0;
int deltaRecv = 0;

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
int8_t audioRecDataBuff[HS_ISO_IN_ENDP_PACKET_SIZE];

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
int8_t audioPlayDataBuff[HS_ISO_OUT_ENDP_PACKET_SIZE];

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t usbAudioFeedBackBuffer[4];
USB_RAM_ADDRESS_ALIGNMENT(4) uint8_t audioFeedBackBuffer[4];
volatile uint8_t feedbackValueUpdating;

usb_device_composite_struct_t* g_deviceAudioComposite;
extern usb_device_composite_struct_t g_composite;

typedef struct DataBuff
{
    uint8_t* buf;
    uint16_t len;
    uint16_t rdidx;
    uint16_t wridx;
    uint8_t size;
} DataBuff;

DataBuff audioRecBuffer = { .buf = (uint8_t*)audioRecBuff,
                            .len = sizeof(audioRecBuff),
                            .wridx = 0,
                            .rdidx = 0,
                            .size = sizeof(audioRecBuff[0]) };

DataBuff audioPlayBuffer = { .buf = (uint8_t*)audioPlayBuff,
                             .len = sizeof(audioPlayBuff),
                             .wridx = 0,
                             .rdidx = 0,
                             .size = sizeof(audioPlayBuff[0]) };

void dataBuffGetData(DataBuff* buf, uint8_t* dest, int bytes)
{
    // TODO this use case
    if (bytes > buf->len)
        return;
    if (buf->rdidx + bytes < buf->len)
    {
        memcpy(&dest[0], &buf->buf[buf->rdidx], bytes);
        buf->rdidx += bytes;
    }
    else
    {
        int endlen = buf->len - buf->rdidx;
        memcpy(&dest[0], &buf->buf[buf->rdidx], endlen);
        buf->rdidx = bytes - endlen;
        memcpy(&dest[endlen], &buf->buf[0], buf->rdidx);
    }
}

void dataBuffSetData(DataBuff* buf, uint8_t* src, int bytes)
{
    // TODO this use case
    if (bytes > buf->len)
        return;

    if (buf->wridx + bytes < buf->len)
    {
        memcpy(&buf->buf[buf->wridx], &src[0], bytes);
        buf->wridx += bytes;
    }
    else
    {
        int endlen = buf->len - buf->wridx;
        memcpy(&buf->buf[buf->wridx], &src[0], endlen);
        buf->wridx = bytes - endlen;
        memcpy(&buf->buf[0], &src[endlen], buf->wridx);
    }
}

int dataBuffSize(DataBuff* buf)
{
    return buf->wridx - buf->rdidx;
}

static bool usbAudioBlockActive = false;

void resetUsbAudioBuffer()
{
#if defined(USB_LOG_ENABLED) && USB_LOG_ENABLED == 1
    LOGF("USB Audio buffer reset\n\r"
         "min buffer = %d, max buffer = %d\n\r",
         minToSend,
         maxToSend);
#endif
    minToSend = UINT32_MAX;
    maxToSend = 0;
    audioToSend = 0;
    samplesToSend = 0;
    audioRecWriteOffset = 0;
    audioRecBuffer.rdidx = 0;
    audioRecBuffer.wridx = 0;
    usbIsoReady = false;
    usbAudioBlockActive = false;
    audioReady = false;
}

bool isUsbAudioOutputActive(void)
{
    return usbAudioBlockActive && usbIsoReady && g_deviceAudioComposite != NULL &&
           g_deviceAudioComposite->audioUnified.attach != 0;
}

void beginUsbAudioFill()
{
    USB_DeviceIsrDisable();

    const bool active = usbIsoReady && g_deviceAudioComposite != NULL &&
                        g_deviceAudioComposite->audioUnified.attach != 0;
    usbAudioBlockActive = active;
    if (active && audioToSend > (AUDIO_BUFFERS - 1) * SAMPLES_CODEC)
        resetUsbAudioBuffer();

    USB_DeviceIsrEnable();
}

#define DIV_P 8
#define DIV_D 4

void finishUsbAudioFill()
{
    if (!usbAudioBlockActive)
        return;

    USB_DeviceIsrDisable();

    if (usbAudioBlockActive && usbIsoReady)
    {
        if (audioReady)
        {
            delta = sent - SAMPLES_CODEC;
            sent -= SAMPLES_CODEC;
            minToSend = MIN(audioToSend, minToSend);
            maxToSend = MAX(audioToSend, maxToSend);
            /*
            currError = audioToSend - SAMPLES_CODEC;
            currSampleRate =
              CODEC_SAMPLE_RATE + currError / DIV_P + (currError - prevError) / DIV_D;
            prevError = currError;
           */
        }

        audioToSend += SAMPLES_CODEC;
        audioRecWriteOffset += SAMPLES_REC_COUNT;
        audioRecWriteOffset %= SAMPLES_REC_COUNT * AUDIO_BUFFERS;

        if (audioToSend > (AUDIO_BUFFERS - 2) * SAMPLES_CODEC)
            audioReady = true;
    }

    usbAudioBlockActive = false;
    USB_DeviceIsrEnable();
}

void fillUsbAudioChannelData(void* data, int count, int channel)
{
    if (!usbAudioBlockActive || !usbIsoReady || channel >= CHANNELS_REC_COUNT)
        return;

    USB_DeviceIsrDisable();

    if (usbAudioBlockActive && usbIsoReady)
    {
        for (int i = 0; i < count; i++)
        {
            audioRecBuff[audioRecWriteOffset + i * CHANNELS_REC_COUNT + channel] =
              ((int16_t*)data)[i];
        }
    }

    USB_DeviceIsrEnable();
}

void updateUsbOutputBuffers(void* left, void* right, int count)
{
    USB_DeviceIsrDisable();

    if (audioPlayReady && samplesToRecv > 0)
    {
        for (int i = 0; i < count; i++)
        {
            ((int16_t*)left)[i] = audioPlayBuff[audioPlayBuffer.rdidx + i * CHANNELS_PLAY_COUNT];
            ((int16_t*)right)[i] =
              audioPlayBuff[audioPlayBuffer.rdidx + i * CHANNELS_PLAY_COUNT + 1];
        }
        samplesToRecv -= SAMPLES_CODEC;
        deltaRecv = samplesToRecv - SAMPLES_CODEC;
        audioPlayBuffer.rdidx += SAMPLES_PLAY_COUNT;
        audioPlayBuffer.rdidx %= SAMPLES_PLAY_COUNT * AUDIO_BUFFERS;
    }

    USB_DeviceIsrEnable();
}

bool updateUsbInputBuffers(void* data, int count)
{
    bool hasData = false;
    USB_DeviceIsrDisable();

    if (audioPlayReady && samplesToRecv > 0)
    {
        for (int i = 0; i < count; i++)
        {
            ((int16_t*)data)[2 * i] =
              audioPlayBuff[audioPlayBuffer.rdidx + i * CHANNELS_PLAY_COUNT];
            ((int16_t*)data)[2 * i + 1] =
              audioPlayBuff[audioPlayBuffer.rdidx + i * CHANNELS_PLAY_COUNT + 1];
        }
        samplesToRecv -= SAMPLES_CODEC;
        deltaRecv = samplesToRecv - SAMPLES_CODEC;
        audioPlayBuffer.rdidx += SAMPLES_PLAY_COUNT;
        audioPlayBuffer.rdidx %= SAMPLES_PLAY_COUNT * AUDIO_BUFFERS;
        hasData = true;
    }

    USB_DeviceIsrEnable();
    return hasData;
}
/*!
 * @brief device Audio callback function.
 *
 * This function handle the Audio class specified event.
 * @param handle          The USB class  handle.
 * @param event           The USB device event type.
 * @param param           The parameter of the class specific event.
 * @return kStatus_USB_Success or error.
 */
usb_status_t USB_DeviceAudioCompositeCallback(class_handle_t handle, uint32_t event, void* param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    usb_device_endpoint_callback_message_struct_t* ep_cb_param;
    ep_cb_param = (usb_device_endpoint_callback_message_struct_t*)param;
    usb_device_control_request_struct_t* request = (usb_device_control_request_struct_t*)param;
    uint32_t epPacketSize = 0;
    samplesToSend = 0;
    expectedFrameRate = (float)(currSampleRate) / (float)(USB_FRAMES_PER_SEC);

    switch (event)
    {
        case kUSB_DeviceAudioEventStreamSendResponse:
            /* endpoint callback length is USB_CANCELLED_TRANSFER_LENGTH (0xFFFFFFFFU) when
             * transfer is canceled */

            if ((g_deviceAudioComposite->audioUnified.attach) &&
                (ep_cb_param->length != (USB_CANCELLED_TRANSFER_LENGTH)))
            {
                if (ep_cb_param->length == HS_ISO_FEEDBACK_ENDP_PACKET_SIZE)
                {
                    uint32_t rate =
                      (float)((float)(CODEC_SAMPLE_RATE - deltaRecv) / (float)USB_FRAMES_PER_SEC) *
                      0xFFFF;

                    memcpy(usbAudioFeedBackBuffer, &rate, sizeof(rate));

                    error =
                      USB_DeviceAudioSend(g_deviceAudioComposite->audioUnified.audioPlayerHandle,
                                          USB_AUDIO_PLAYER_FEEDBACK_ENDPOINT,
                                          usbAudioFeedBackBuffer,
                                          HS_ISO_FEEDBACK_ENDP_PACKET_SIZE);
                }
                else
                {
                    usbIsoReady = true;
                    if (audioReady)
                    {
                        currFrameCount++;
                        /*
                        if (currFrameCount % 2)
                        {
                            samplesToSend = SAMPLES_PART_EVEN;
                        }
                        else
                        {
                            samplesToSend = SAMPLES_PART_ODD;
                        }
                        */

                        samplesToSend = SAMPLES_PART_ODD;

                        currSampleCount += samplesToSend;
                        currFrameRate = (float)currSampleCount / (float)currFrameCount;

                        if (delta < 0)
                        {
                            samplesToSend += 1;
                            delta += 1;
                        }
                        else if (delta > 0)
                        {
                            samplesToSend -= 1;
                            delta -= 1;
                        }

                        if (audioToSend < samplesToSend)
                        {
                            // assert(0);
#if defined(USB_LOG_ENABLED) && USB_LOG_ENABLED == 1
                            LOGF("Not enough data %d\n\r", audioToSend);
#endif
                            samplesToSend = audioToSend;
                        }

                        sent += samplesToSend;
                        currSampleRate += samplesToSend;

                        audioToSend -= samplesToSend;
                        epPacketSize = samplesToSend * CHANNELS_REC_COUNT * AUDIO_IN_FORMAT_SIZE;
                        dataBuffGetData(&audioRecBuffer, (uint8_t*)audioRecDataBuff, epPacketSize);

                        if (currFrameCount == USB_FRAMES_PER_SEC)
                        {
#if defined(USB_LOG_ENABLED) && USB_LOG_ENABLED == 1
                            LOGF("min buffer = %d\n\rmax buffer = %d\n\r"
                                 "curr sample rate = %d\n\r",
                                 minToSend,
                                 maxToSend,
                                 currSampleRate);
#endif
                            currSampleCount = 0;
                            currFrameCount = 0;
                            currSampleRate = 0;
                        }
                        /*
                         * generate sine from usb irq
                         */
                        // dataBuffGetData(&sineBuffer, (uint8_t*)audioRecDataBuff, epPacketSize);
                    }
                    error =
                      USB_DeviceAudioSend(g_deviceAudioComposite->audioUnified.audioRecorderHandle,
                                          USB_AUDIO_RECORDER_STREAM_ENDPOINT,
                                          (uint8_t*)audioRecDataBuff,
                                          epPacketSize);
                }
            }
            break;
        case kUSB_DeviceAudioEventStreamRecvResponse:
            if ((g_deviceAudioComposite->audioUnified.attach) &&
                (ep_cb_param->length != (USB_CANCELLED_TRANSFER_LENGTH)))
            {
                dataBuffSetData(&audioPlayBuffer, (uint8_t*)audioPlayDataBuff, ep_cb_param->length);
                samplesToRecv +=
                  ep_cb_param->length / (CHANNELS_PLAY_COUNT * AUDIO_OUT_FORMAT_SIZE);

                if (samplesToRecv > 2 * SAMPLES_CODEC)
                {
                    audioPlayReady = true;
                }
                error = USB_DeviceAudioRecv(g_deviceAudioComposite->audioUnified.audioPlayerHandle,
                                            USB_AUDIO_PLAYER_STREAM_ENDPOINT,
                                            (uint8_t*)audioPlayDataBuff,
                                            HS_ISO_OUT_ENDP_PACKET_SIZE);
            }
            break;
        case USB_DEVICE_AUDIO_CS_GET_RANGE_SAMPLING_FREQ_CONTROL:
            request->buffer = (uint8_t*)&g_deviceAudioComposite->audioUnified.freqControlRange;
            request->length = sizeof(g_deviceAudioComposite->audioUnified.freqControlRange);
            error = kStatus_USB_Success;
            break;
        case USB_DEVICE_AUDIO_CS_GET_CUR_SAMPLING_FREQ_CONTROL:
            request->buffer = (uint8_t*)&g_deviceAudioComposite->audioUnified.curSampleFrequency;
            request->length = sizeof(g_deviceAudioComposite->audioUnified.curSampleFrequency);
            error = kStatus_USB_Success;
            break;
        case USB_DEVICE_AUDIO_CS_GET_CUR_CLOCK_VALID_CONTROL:
            request->buffer = &g_deviceAudioComposite->audioUnified.curClockValid;
            request->length = sizeof(g_deviceAudioComposite->audioUnified.curClockValid);
            error = kStatus_USB_Success;
            break;
        case USB_DEVICE_AUDIO_CS_SET_CUR_SAMPLING_FREQ_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer =
                  (uint8_t*)&g_deviceAudioComposite->audioUnified.curSampleFrequency;
                request->length = sizeof(g_deviceAudioComposite->audioUnified.curSampleFrequency);
            }
            error = kStatus_USB_Success;
            break;
        case USB_DEVICE_AUDIO_CS_SET_CUR_CLOCK_VALID_CONTROL:
            if (request->isSetup == 1U)
            {
                request->buffer = &g_deviceAudioComposite->audioUnified.curClockValid;

                request->length = sizeof(g_deviceAudioComposite->audioUnified.curClockValid);
            }
            error = kStatus_USB_Success;
            break;
        default:
            break;
    }

    return error;
}
/* The USB_DeviceAudioRecorderStatusReset() function resets the audio recorder status to
 * the initialized status */
void USB_DeviceAudioRecorderStatusReset()
{
    resetUsbAudioBuffer();
    g_deviceAudioComposite->audioUnified.startRec = 0;
    g_deviceAudioComposite->audioUnified.startRecHalfFull = 0;
    g_deviceAudioComposite->audioUnified.audioRecvCount = 0;
    g_deviceAudioComposite->audioUnified.usbSendTimes = 0;
    g_deviceAudioComposite->audioUnified.tdWriteNumberRec = 0;
    g_deviceAudioComposite->audioUnified.tdReadNumberRec = 0;
    g_deviceAudioComposite->audioUnified.recorderReservedSpace = 0;
}

void USB_DeviceAudioPlayerStatusReset()
{
    samplesToRecv = 0;
    audioPlayReady = 0;
    audioPlayReadOffset = 0;
    audioPlayBuffer.rdidx = 0;
    audioPlayBuffer.wridx = 0;
}
/*!
 * @brief Audio set configuration function.
 *
 * This function sets configuration for msc class.
 *
 * @param handle The Audio class handle.
 * @param configure The Audio class configure index.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceAudioCompositeSetConfigure(class_handle_t handle, uint8_t configure)
{
    USB_DeviceAudioRecorderStatusReset();
    if (USB_COMPOSITE_CONFIGURE_INDEX == configure)
    {
        g_deviceAudioComposite->audioUnified.attach = 1U;
    }
#if defined(USB_LOG_ENABLED) && USB_LOG_ENABLED == 1
    LOGF("USB Audio SetConfigure %d\n\r", configure);
#endif
    return kStatus_USB_Success;
}

usb_status_t USB_DeviceAudioRecorderSetInterface(class_handle_t handle,
                                                 uint8_t interface,
                                                 uint8_t alternateSetting)
{
    usb_status_t error = kStatus_USB_Success;

    USB_DeviceAudioRecorderStatusReset();
    if (alternateSetting == USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_1)
    {
        error = USB_DeviceAudioSend(g_deviceAudioComposite->audioUnified.audioRecorderHandle,
                                    USB_AUDIO_RECORDER_STREAM_ENDPOINT,
                                    (uint8_t*)&audioRecDataBuff[0],
                                    HS_ISO_IN_ENDP_PACKET_SIZE);
        g_deviceAudioComposite->audioUnified.startRec = 1;
    }
#if defined(USB_LOG_ENABLED) && USB_LOG_ENABLED == 1
    LOGF("USB Audio SetInterface %d\n\r", alternateSetting);
#endif
    return error;
}

usb_status_t USB_DeviceAudioPlayerSetInterface(class_handle_t handle,
                                               uint8_t interface,
                                               uint8_t alternateSetting)
{
    usb_status_t error = kStatus_USB_Success;

    USB_DeviceAudioPlayerStatusReset();
    if (alternateSetting == USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_1)
    {
        error = USB_DeviceAudioRecv(g_deviceAudioComposite->audioUnified.audioPlayerHandle,
                                    USB_AUDIO_PLAYER_STREAM_ENDPOINT,
                                    (uint8_t*)&audioPlayDataBuff[0],
                                    HS_ISO_OUT_ENDP_PACKET_SIZE);
        g_deviceAudioComposite->audioUnified.startPlayFlag = 1;

        if (error != kStatus_USB_Success)
        {
            return error;
        }
        else
        {
            error = USB_DeviceAudioSend(g_deviceAudioComposite->audioUnified.audioPlayerHandle,
                                        USB_AUDIO_PLAYER_FEEDBACK_ENDPOINT,
                                        usbAudioFeedBackBuffer,
                                        HS_ISO_FEEDBACK_ENDP_PACKET_SIZE);
        }
    }
#if defined(USB_LOG_ENABLED) && USB_LOG_ENABLED == 1
    LOGF("USB Audio SetInterface %d\n\r", alternateSetting);
#endif
    return error;
}
/*!
 * @brief Audio init function.
 *
 * This function initializes the device with the composite device class information.
 *
 * @param device_composite          The pointer to the composite device structure.
 * @return kStatus_USB_Success .
 */
usb_status_t USB_DeviceAudioCompositeInit(usb_device_composite_struct_t* device_composite)
{
#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
    CTIMER_CaptureInit();
#else
#endif
    audioToSend = 0;
    samplesToSend = 0;
    audioRecWriteOffset = 0;
    audioRecBuffer.rdidx = 0;
    audioRecBuffer.wridx = 0;
    g_deviceAudioComposite = device_composite;
    g_deviceAudioComposite->audioUnified.copyProtect = 0x01U;
    g_deviceAudioComposite->audioUnified.curSpeakerMute = 0x00U;
    g_deviceAudioComposite->audioUnified.curMicrophoneMute = 0x00U;
    g_deviceAudioComposite->audioUnified.curSpeakerVolume[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.curSpeakerVolume[1] = 0x1fU;
    g_deviceAudioComposite->audioUnified.curMicrophoneVolume[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.curMicrophoneVolume[1] = 0x1fU;
    g_deviceAudioComposite->audioUnified.minVolume[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.minVolume[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.maxVolume[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.maxVolume[1] = 0X43U;
    g_deviceAudioComposite->audioUnified.resVolume[0] = 0x01U;
    g_deviceAudioComposite->audioUnified.resVolume[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.curBass = 0x00U;
    g_deviceAudioComposite->audioUnified.curBass = 0x00U;
    g_deviceAudioComposite->audioUnified.minBass = 0x80U;
    g_deviceAudioComposite->audioUnified.maxBass = 0x7FU;
    g_deviceAudioComposite->audioUnified.resBass = 0x01U;
    g_deviceAudioComposite->audioUnified.curMid = 0x00U;
    g_deviceAudioComposite->audioUnified.minMid = 0x80U;
    g_deviceAudioComposite->audioUnified.maxMid = 0x7FU;
    g_deviceAudioComposite->audioUnified.resMid = 0x01U;
    g_deviceAudioComposite->audioUnified.curTreble = 0x01U;
    g_deviceAudioComposite->audioUnified.minTreble = 0x80U;
    g_deviceAudioComposite->audioUnified.maxTreble = 0x7FU;
    g_deviceAudioComposite->audioUnified.resTreble = 0x01U;
    g_deviceAudioComposite->audioUnified.curAutomaticGain = 0x01U;
    g_deviceAudioComposite->audioUnified.curDelay[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.curDelay[1] = 0x40U;
    g_deviceAudioComposite->audioUnified.minDelay[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.minDelay[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.maxDelay[0] = 0xFFU;
    g_deviceAudioComposite->audioUnified.maxDelay[1] = 0xFFU;
    g_deviceAudioComposite->audioUnified.resDelay[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.resDelay[1] = 0x01U;
    g_deviceAudioComposite->audioUnified.curLoudness = 0x01U;
    g_deviceAudioComposite->audioUnified.curSamplingFrequency[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.curSamplingFrequency[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.curSamplingFrequency[2] = 0x01U;
    g_deviceAudioComposite->audioUnified.minSamplingFrequency[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.minSamplingFrequency[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.minSamplingFrequency[2] = 0x01U;
    g_deviceAudioComposite->audioUnified.maxSamplingFrequency[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.maxSamplingFrequency[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.maxSamplingFrequency[2] = 0x01U;
    g_deviceAudioComposite->audioUnified.resSamplingFrequency[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.resSamplingFrequency[1] = 0x00U;
    g_deviceAudioComposite->audioUnified.resSamplingFrequency[2] = 0x01U;
    g_deviceAudioComposite->audioUnified.curSpeakerMute20 = 0U;
    g_deviceAudioComposite->audioUnified.curMicrophoneMute20 = 0U;
    g_deviceAudioComposite->audioUnified.curClockValid = 1U;
    g_deviceAudioComposite->audioUnified.curSpeakerVolume20[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.curSpeakerVolume20[1] = 0x1FU;
    g_deviceAudioComposite->audioUnified.curMicrophoneVolume20[0] = 0x00U;
    g_deviceAudioComposite->audioUnified.curMicrophoneVolume20[1] = 0x1FU;
    g_deviceAudioComposite->audioUnified.curSampleFrequency = AUDIO_IN_SAMPLING_RATE_KHZ;
    g_deviceAudioComposite->audioUnified.tdReadNumberPlay = 0;
    g_deviceAudioComposite->audioUnified.tdWriteNumberPlay = 0;
    g_deviceAudioComposite->audioUnified.tdWriteNumberRec = 0;
    g_deviceAudioComposite->audioUnified.tdReadNumberRec = 0;
    g_deviceAudioComposite->audioUnified.audioSendCount[0] = 0;
    g_deviceAudioComposite->audioUnified.audioSendCount[1] = 0;
    g_deviceAudioComposite->audioUnified.lastAudioSendCount = 0;
    g_deviceAudioComposite->audioUnified.usbRecvCount = 0;
    g_deviceAudioComposite->audioUnified.audioSendTimes = 0;
    g_deviceAudioComposite->audioUnified.usbRecvTimes = 0;
    g_deviceAudioComposite->audioUnified.audioRecvCount = 0;
    g_deviceAudioComposite->audioUnified.usbSendTimes = 0;
    g_deviceAudioComposite->audioUnified.startPlayFlag = 0;
    g_deviceAudioComposite->audioUnified.startPlayHalfFull = 0;
    g_deviceAudioComposite->audioUnified.startRec = 0;
    g_deviceAudioComposite->audioUnified.startRecHalfFull = 0;
    g_deviceAudioComposite->audioUnified.speakerIntervalCount = 0;
    g_deviceAudioComposite->audioUnified.speakerReservedSpace = 0;
    g_deviceAudioComposite->audioUnified.recorderReservedSpace = 0;
    g_deviceAudioComposite->audioUnified.timesFeedbackCalculate = 0;
    g_deviceAudioComposite->audioUnified.speakerDetachOrNoInput = 0;

    g_deviceAudioComposite->audioUnified.freqControlRange.wNumSubRanges = 1U;
    g_deviceAudioComposite->audioUnified.freqControlRange.wMIN = AUDIO_IN_SAMPLING_RATE_KHZ;
    g_deviceAudioComposite->audioUnified.freqControlRange.wMAX = AUDIO_IN_SAMPLING_RATE_KHZ;
    g_deviceAudioComposite->audioUnified.freqControlRange.wRES = 0U;
#if defined(USB_DEVICE_AUDIO_USE_SYNC_MODE) && (USB_DEVICE_AUDIO_USE_SYNC_MODE > 0U)
#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
    g_deviceAudioComposite->audioUnified.froTrimIntervalCount = 0;
    g_deviceAudioComposite->audioUnified.usbFroTicksPrev = 0;
    g_deviceAudioComposite->audioUnified.usbFroTicksEma = AUDIO_FRO_USB_SOF_INTERVAL_TICK_COUNT;
    g_deviceAudioComposite->audioUnified.usbFroTickEmaFrac = 0;
    g_deviceAudioComposite->audioUnified.usbFroTickBasedPrecision =
      AUDIO_USB_FRO_TRIM_TICK_BASED_PRECISION;
#endif
    g_deviceAudioComposite->audioUnified.curAudioPllFrac = AUDIO_PLL_FRACTIONAL_DIVIDER;
    g_deviceAudioComposite->audioUnified.audioPllTicksPrev = 0;
    g_deviceAudioComposite->audioUnified.audioPllTicksEma = AUDIO_PLL_USB_SOF_INTERVAL_TICK_COUNT;
    g_deviceAudioComposite->audioUnified.audioPllTickEmaFrac = 0;
    g_deviceAudioComposite->audioUnified.audioPllTickBasedPrecision =
      AUDIO_PLL_FRACTION_TICK_BASED_PRECISION;
    g_deviceAudioComposite->audioUnified.stopDataLengthAudioAdjust = 0U;
#endif
    return kStatus_USB_Success;
}
