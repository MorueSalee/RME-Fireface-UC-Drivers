#ifndef __rme_uc_h__
#define __rme_uc_h__

#include <libusb-1.0/libusb.h>

/*
 *  Older firmwares (e.g. v118) enumerate with the SMSC vendor ID,
 *  newer ones with RME's own vendor ID. Only the AP (Mac) mode is supported.
 */
#define RME_UC_VENDOR_OLD	0x0424  /*  1060, firmware <= v118 */
#define RME_UC_VENDOR		0x2a39  /* 10809, newer firmwares  */
#define RME_UC_PRODUCT		0x3fc6  /* 16326, AP (Mac) mode    */
#define RME_UC_PRODUCT_PC	0x3fce  /* PC (Win) mode on 0x2a39, not supported */


// int rme_init_iface( libusb_device_handle *dev );

#endif
