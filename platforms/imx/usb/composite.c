/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 - 2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
/*${standard_header_anchor}*/
#include "usb_device_config.h"

#include "interruptsConfig.h"

#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"

#include "usb_device_cdc_acm.h"
#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"
#include "usb_device_midi.h"
#include "usb_device_msc.h"

#include "board.h"
#include "clock_config.h"
#include "composite.h"
#include "fsl_debug_console.h"
#include "fsl_device_registers.h"
#include "pin_mux.h"
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
#include "fsl_sysmpu.h"
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

#if ((defined FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U))
#include "usb_phy.h"
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* USB PHY condfiguration */
#define BOARD_USB_PHY_D_CAL     (0x0CU)
#define BOARD_USB_PHY_TXCAL45DP (0x06U)
#define BOARD_USB_PHY_TXCAL45DM (0x06U)
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void BOARD_InitHardware(void);
void USB_DeviceClockInit(void);
void USB_DeviceIsrEnable(void);
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void* deviceHandle);
#endif

/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle          The USB device handle.
 * @param event           The USB device event type.
 * @param param           The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void* param);

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Composite device structure. */
usb_device_composite_struct_t g_composite;
extern usb_device_class_struct_t g_UsbDeviceCdcVcomConfig;
extern usb_device_class_struct_t g_UsbDeviceMidiClass;
extern usb_device_class_struct_t g_UsbDeviceAudioClassRecorder;
extern usb_device_class_struct_t g_UsbDeviceAudioClassPlayer;
extern usb_device_class_struct_t g_UsbDeviceMscClass;

#define USB_CLASSES_COUNT 5

usb_device_class_config_struct_t g_CompositeClassConfig[USB_CLASSES_COUNT] = {
    {
      USB_DeviceMidiCallback,
      (class_handle_t)NULL,
      &g_UsbDeviceMidiClass,
    },
    {
      USB_DeviceAudioCompositeCallback,
      (class_handle_t)NULL,
      &g_UsbDeviceAudioClassRecorder,
    },
    {
      USB_DeviceAudioCompositeCallback,
      (class_handle_t)NULL,
      &g_UsbDeviceAudioClassPlayer,
    },
    {
      USB_DeviceMscCallback,
      (class_handle_t)NULL,
      &g_UsbDeviceMscClass,
    },
    {
      USB_DeviceCdcVcomCallback,
      (class_handle_t)NULL,
      &g_UsbDeviceCdcVcomConfig,
    },
};

/* USB device class configuration information */
usb_device_class_config_list_struct_t g_UsbDeviceCompositeClassConfigList = {
    g_CompositeClassConfig,
    USB_DeviceCallback,
    USB_CLASSES_COUNT
};

extern serial_handle_t g_serialHandle;

usb_composite_state_t g_UsbState = { .isMidiEnabled = true,
                                     .isAudioInEnabled = true,
                                     .isAudioOutEnabled = true,
                                     .isStorageEnabled = false,
                                     .isSerialEnabled = false };

/*******************************************************************************
 * Code
 ******************************************************************************/

void USB_OTG1_IRQHandler(void)
{
    USB_DeviceEhciIsrFunction(g_composite.deviceHandle);
    /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
    __DSB();
}

void USB_OTG2_IRQHandler(void)
{
    USB_DeviceEhciIsrFunction(g_composite.deviceHandle);
    /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
    __DSB();
}
/* MAX line current source*/
#define BOARD_USB_PHY_D_CAL_MAX 0x0
/* MIN DP and DM line resistance */
#define BOARD_USB_PHY_TXCAL45DP_MIN 0xF
#define BOARD_USB_PHY_TXCAL45DM_MIN 0xF

void USB_DeviceClockInit(void)
{
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL_MAX,
        BOARD_USB_PHY_TXCAL45DP_MIN,
        BOARD_USB_PHY_TXCAL45DM_MIN,
    };

    if (CONTROLLER_ID == kUSB_ControllerEhci0)
    {
        CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
        CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000U);
    }
    else
    {
        CLOCK_EnableUsbhs1PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
        CLOCK_EnableUsbhs1Clock(kCLOCK_Usb480M, 480000000U);
    }
    USB_EhciPhyInit(CONTROLLER_ID, 0, &phyConfig);
}
void USB_DeviceIsrEnable(void)
{
    uint8_t irqNumber;

    uint8_t usbDeviceEhciIrq[] = USBHS_IRQS;
    irqNumber = usbDeviceEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];

    /* Install isr, set priority, and enable IRQ. */
    NVIC_SetPriority((IRQn_Type)irqNumber, kUSBDevicePriorityLevel);
    EnableIRQ((IRQn_Type)irqNumber);
}

void USB_DeviceIsrDisable(void)
{
    uint8_t irqNumber;

    uint8_t usbDeviceEhciIrq[] = USBHS_IRQS;
    irqNumber = usbDeviceEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];

    DisableIRQ((IRQn_Type)irqNumber);
}

#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void* deviceHandle)
{
    USB_DeviceEhciTaskFunction(deviceHandle);
}
#endif

void USB_DeviceReset()
{
    USB_DeviceStop(g_composite.deviceHandle);
    SDK_DelayAtLeastUs(5000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    USB_DeviceRun(g_composite.deviceHandle);
}

/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle          The USB device handle.
 * @param event           The USB device event type.
 * @param param           The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void* param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    uint16_t* temp16 = (uint16_t*)param;
    uint8_t* temp8 = (uint8_t*)param;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
        {
            g_composite.attach = 0;
            g_composite.currentConfiguration = 0U;
            g_composite.mscDisk.stop = 0U;
            error = kStatus_USB_Success;
#if (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)) ||                          \
  (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* Get USB speed to configure the device, including max packet size and interval of the
             * endpoints. */
            if (kStatus_USB_Success == USB_DeviceClassGetSpeed(CONTROLLER_ID, &g_composite.speed))
            {
                USB_DeviceSetSpeed(handle, g_composite.speed);
            }
#endif
        }
        break;
        case kUSB_DeviceEventSetConfiguration:
            if (0U == (*temp8))
            {
                g_composite.attach = 0;
                g_composite.currentConfiguration = 0U;
                error = kStatus_USB_Success;
            }
            else if (USB_COMPOSITE_CONFIGURE_INDEX == (*temp8))
            {
                g_composite.attach = 1;
                g_composite.currentConfiguration = *temp8;
                if (g_UsbState.isSerialEnabled)
                {
                    USB_DeviceCdcVcomSetConfigure(g_composite.cdcVcom.cdcAcmHandle, *temp8);
                }
                if (g_UsbState.isMidiEnabled)
                {
                    USB_DeviceMidiSetConfigure(g_composite.midi.midiHandle, *temp8);
                }
                if (g_UsbState.isStorageEnabled)
                {
                    USB_DeviceMscDiskSetConfigure(g_composite.mscDisk.mscHandle, *temp8);
                }
                if (g_UsbState.isAudioInEnabled || g_UsbState.isAudioOutEnabled)
                {
                    USB_DeviceAudioCompositeSetConfigure(
                      g_composite.audioUnified.audioRecorderHandle, *temp8);
                }
                error = kStatus_USB_Success;
            }
            else
            {
                /* no action, return kStatus_USB_InvalidRequest */
            }
            break;
        case kUSB_DeviceEventSetInterface:
            if (g_composite.attach)
            {
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                uint8_t alternateSetting = (uint8_t)(*temp16 & 0x00FFU);

                if (interface == g_cdcVcomInterfaces[0].interfaceNumber)
                {
                    if (alternateSetting < USB_CDC_VCOM_CIC_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_composite.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error = kStatus_USB_Success;
                    }
                }
                else if (interface == g_cdcVcomInterfaces[1].interfaceNumber)
                {
                    if (alternateSetting < USB_CDC_VCOM_DIC_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_composite.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error = kStatus_USB_Success;
                    }
                }
                else if (interface == g_MidiInterfaces[0].interfaceNumber)
                {
                    if (alternateSetting < USB_MIDI_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_composite.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error = kStatus_USB_Success;
                    }
                }
                else if (interface == g_mscDiskInterfaces[0].interfaceNumber)
                {
                    if (alternateSetting < USB_MSC_DISK_INTERFACE_ALTERNATE_COUNT)
                    {
                        g_composite.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error = kStatus_USB_Success;
                    }
                }
                else if ((interface == g_UsbDeviceAudioPlayerInterfaces[0].interfaceNumber) ||
                         (interface == g_UsbDeviceAudioRecorderInterfaces[0].interfaceNumber))
                {
                    if (alternateSetting < USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_COUNT)
                    {
                        error = kStatus_USB_Success;
                    }
                }
                else if (interface == g_UsbDeviceAudioRecorderInterfaces[1].interfaceNumber)
                {
                    if (alternateSetting < USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_COUNT)
                    {
                        error = USB_DeviceAudioRecorderSetInterface(
                          g_composite.audioUnified.audioRecorderHandle,
                          interface,
                          alternateSetting);
                    }
                }
                else if (interface == g_UsbDeviceAudioPlayerInterfaces[1].interfaceNumber)
                {
                    if (alternateSetting < USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_COUNT)
                    {
                        error = USB_DeviceAudioPlayerSetInterface(
                          g_composite.audioUnified.audioPlayerHandle, interface, alternateSetting);
                    }
                }
                else
                {
                    /* no action, return kStatus_USB_InvalidRequest */
                }
            }
            break;
        case kUSB_DeviceEventGetConfiguration:
            if (param)
            {
                *temp8 = g_composite.currentConfiguration;
                error = kStatus_USB_Success;
            }
            break;
        case kUSB_DeviceEventGetInterface:
            if (param)
            {
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                if (interface < USB_INTERFACE_COUNT)
                {
                    *temp16 =
                      (*temp16 & 0xFF00U) | g_composite.currentInterfaceAlternateSetting[interface];
                    error = kStatus_USB_Success;
                }
            }
            break;
        case kUSB_DeviceEventGetDeviceDescriptor:
            if (param)
            {
                error = USB_DeviceGetDeviceDescriptor(
                  handle, (usb_device_get_device_descriptor_struct_t*)param);
            }
            break;
        case kUSB_DeviceEventGetConfigurationDescriptor:
            if (param)
            {
                error = USB_DeviceGetConfigurationDescriptor(
                  handle, (usb_device_get_configuration_descriptor_struct_t*)param);
            }
            break;
#if (defined(USB_DEVICE_CONFIG_CV_TEST) && (USB_DEVICE_CONFIG_CV_TEST > 0U))
        case kUSB_DeviceEventGetDeviceQualifierDescriptor:
            if (param)
            {
                /* Get device descriptor request */
                error = USB_DeviceGetDeviceQualifierDescriptor(
                  handle, (usb_device_get_device_qualifier_descriptor_struct_t*)param);
            }
            break;
#endif
        case kUSB_DeviceEventGetStringDescriptor:
            if (param)
            {
                error = USB_DeviceGetStringDescriptor(
                  handle, (usb_device_get_string_descriptor_struct_t*)param);
            }
            break;
        default:
            break;
    }

    return error;
}

/*!
 * @brief Application initialization function.
 *
 * This function initializes the application.
 *
 * @return None.
 */
void USB_Init()
{
    uint8_t configNum = 0;

    USB_DeviceClockInit();
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

    g_composite.speed = USB_SPEED_HIGH;
    g_composite.attach = 0;
    g_composite.cdcVcom.cdcAcmHandle = (class_handle_t)NULL;
    g_composite.midi.midiHandle = (class_handle_t)NULL;
    g_composite.audioUnified.audioRecorderHandle = (class_handle_t)NULL;
    g_composite.mscDisk.mscHandle = (class_handle_t)NULL;
    g_composite.deviceHandle = NULL;

    if (kStatus_USB_Success != USB_DeviceClassInit(CONTROLLER_ID,
                                                   &g_UsbDeviceCompositeClassConfigList,
                                                   &g_composite.deviceHandle))
    {
        usb_echo("USB device composite demo init failed\r\n");
        return;
    }
    else
    {
        usb_echo("USB device composite demo\r\n");
        g_composite.midi.midiHandle =
          g_UsbDeviceCompositeClassConfigList.config[configNum++].classHandle;
        g_composite.audioUnified.audioRecorderHandle =
          g_UsbDeviceCompositeClassConfigList.config[configNum++].classHandle;
        g_composite.audioUnified.audioPlayerHandle =
          g_UsbDeviceCompositeClassConfigList.config[configNum++].classHandle;
        g_composite.mscDisk.mscHandle =
          g_UsbDeviceCompositeClassConfigList.config[configNum++].classHandle;
        g_composite.cdcVcom.cdcAcmHandle =
          g_UsbDeviceCompositeClassConfigList.config[configNum++].classHandle;

        USB_DeviceCdcVcomInit(&g_composite);
        USB_DeviceMidiImplInit(&g_composite);
        USB_DeviceAudioCompositeInit(&g_composite);
        USB_DeviceMscDiskInit(&g_composite);
    }

    USB_DeviceIsrEnable();

    /*Add one delay here to make the DP pull down long enough to allow host to detect the
     * previous disconnection.*/
    SDK_DelayAtLeastUs(5000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
    USB_DeviceRun(g_composite.deviceHandle);
}

/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */
void USB_Task()
{
#if USB_DEVICE_CONFIG_USE_TASK
    USB_DeviceTaskFunction(g_composite.deviceHandle);
#endif
    USB_DeviceCdcVcomTask();
}

/*!
 * @brief Does the work of the USB interrupt and of USB_Task() in one call.
 *
 * For the hard fault report of the development console: after a fault no
 * interrupt is taken and the main loop no longer runs.
 *
 * @return None.
 */
void USB_PollFromFault(void)
{
    USB_DeviceEhciIsrFunction(g_composite.deviceHandle);
    USB_Task();
}
