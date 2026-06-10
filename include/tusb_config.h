#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE

#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

// MSC Endpoint Buffer Size (512 bytes is required for high performance and standard block size)
#define CFG_TUD_MSC_EP_BUFSIZE  512

#endif // _TUSB_CONFIG_H_
