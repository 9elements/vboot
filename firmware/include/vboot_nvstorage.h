/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Non-volatile storage routines for verified boot.
 */

#ifndef VBOOT_REFERENCE_NVSTORAGE_H_
#define VBOOT_REFERENCE_NVSTORAGE_H_

#define VBNV_BLOCK_SIZE 16  /* Size of NV storage block in bytes */

typedef struct VbNvContext {
  /* Raw NV data.  Caller must fill this before calling VbNvSetup(). */
  uint8_t raw[VBNV_BLOCK_SIZE];
  /* Flag indicating whether raw data has changed.  Set by VbNvTeardown() if
   * the raw data has changed and needs to be stored to the underlying
   * non-volatile data store. */
  int raw_changed;

  /* Internal data for NV storage routines.  Caller should not touch
   * these fields. */
  int regenerate_crc;

} VbNvContext;


/* Parameter type for VbNvGet(), VbNvSet(). */
typedef enum VbNvParam {
  /* Parameter values have been reset to defaults (flag for firmware).
   * 0=clear; 1=set. */
  VBNV_FIRMWARE_SETTINGS_RESET = 0,
  /* Parameter values have been reset to defaults (flag for kernel).
   * 0=clear; 1=set. */
  VBNV_KERNEL_SETTINGS_RESET,
  /* Request debug reset on next S3->S0 transition.  0=clear; 1=set. */
  VBNV_DEBUG_RESET_MODE,
  /* Number of times to try booting RW firmware slot B before slot A.
   * Valid range: 0-15. */
  VBNV_TRY_B_COUNT,
  /* Request recovery mode on next boot; see VBNB_RECOVERY_* below for
   * currently defined reason codes.  8-bit value. */
  VBNV_RECOVERY_REQUEST,
  /* Localization index for screen bitmaps displayed by firmware.
   * 8-bit value. */
  VBNV_LOCALIZATION_INDEX,
  /* Field reserved for kernel/user-mode use; 32-bit value. */
  VBNV_KERNEL_FIELD,
  /* Allow booting from USB in developer mode.  0=no, 1=yes. */
  VBNV_DEV_BOOT_USB,
  /* Only boot Google-signed images in developer mode.  0=no, 1=yes. */
  VBNV_DEV_BOOT_SIGNED_ONLY,
  /* Set by userspace to request that RO firmware disable dev-mode on the next
   * boot. This is likely only possible if the dev-switch is virtual. */
  VBNV_DISABLE_DEV_REQUEST,
  /* Set and cleared by vboot to request that the video Option ROM be loaded at
   * boot time, so that BIOS screens can be displayed. 0=no, 1=yes. */
  VBNV_OPROM_NEEDED,
  /* Request that the firmware clear the TPM owner on the next boot. */
  VBNV_CLEAR_TPM_OWNER_REQUEST,
  /* Flag that TPM owner was cleared on request. */
  VBNV_CLEAR_TPM_OWNER_DONE,
} VbNvParam;


/* Recovery reason codes for VBNV_RECOVERY_REQUEST */
/* Recovery not requested. */
#define VBNV_RECOVERY_NOT_REQUESTED   0x00
/* Recovery requested from legacy utility.  (Prior to the NV storage
 * spec, recovery mode was a single bitfield; this value is reserved
 * so that scripts which wrote 1 to the recovery field are
 * distinguishable from scripts whch use the recovery reasons listed
 * here. */
#define VBNV_RECOVERY_LEGACY          0x01
/* User manually requested recovery via recovery button */
#define VBNV_RECOVERY_RO_MANUAL       0x02
/* RW firmware failed signature check (neither RW firmware slot was valid) */
#define VBNV_RECOVERY_RO_INVALID_RW   0x03
/* S3 resume failed */
#define VBNV_RECOVERY_RO_S3_RESUME    0x04
/* TPM error in read-only firmware */
#define VBNV_RECOVERY_RO_TPM_ERROR    0x05
/* Shared data error in read-only firmware */
#define VBNV_RECOVERY_RO_SHARED_DATA  0x06
/* Test error from S3Resume() */
#define VBNV_RECOVERY_RO_TEST_S3      0x07
/* Test error from LoadFirmwareSetup() */
#define VBNV_RECOVERY_RO_TEST_LFS     0x08
/* Test error from LoadFirmware() */
#define VBNV_RECOVERY_RO_TEST_LF      0x09
/* RW firmware failed signature check (neither RW firmware slot was valid).
 * Recovery reason is VBNV_RECOVERY_RO_INVALID_RW_CHECK_MIN + the check value
 * for the slot which came closest to validating; see VBSD_LF_CHECK_* in
 * vboot_struct.h. */
#define VBNV_RECOVERY_RO_INVALID_RW_CHECK_MIN  0x10
#define VBNV_RECOVERY_RO_INVALID_RW_CHECK_MAX  0x1F
/* Firmware boot failure outside of verified boot (RAM init, missing SSD,
 * etc.). */
#define VBNV_RECOVERY_RO_FIRMWARE     0x20
/* Recovery mode TPM initialization requires a system reboot.  The system was
 * already in recovery mode for some other reason when this happened. */
#define VBNV_RECOVERY_RO_TPM_REBOOT   0x21
/* EC software sync - other error */
#define VBNV_RECOVERY_EC_SOFTWARE_SYNC 0x22
/* EC software sync - unable to determine active EC image */
#define VBNV_RECOVERY_EC_UNKNOWN_IMAGE 0x23
/* EC software sync - error obtaining EC image hash */
#define VBNV_RECOVERY_EC_HASH         0x24
/* EC software sync - error obtaining expected EC image */
#define VBNV_RECOVERY_EC_EXPECTED_IMAGE 0x25
/* EC software sync - error updating EC */
#define VBNV_RECOVERY_EC_UPDATE       0x26
/* EC software sync - unable to jump to EC-RW */
#define VBNV_RECOVERY_EC_JUMP_RW      0x27
/* EC software sync - unable to protect / unprotect EC-RW */
#define VBNV_RECOVERY_EC_PROTECT      0x28
/* Unspecified/unknown error in read-only firmware */
#define VBNV_RECOVERY_RO_UNSPECIFIED  0x3F
/* User manually requested recovery by pressing a key at developer
 * warning screen */
#define VBNV_RECOVERY_RW_DEV_SCREEN   0x41
/* No OS kernel detected */
#define VBNV_RECOVERY_RW_NO_OS        0x42
/* OS kernel failed signature check */
#define VBNV_RECOVERY_RW_INVALID_OS   0x43
/* TPM error in rewritable firmware */
#define VBNV_RECOVERY_RW_TPM_ERROR    0x44
/* RW firmware in dev mode, but dev switch is off */
#define VBNV_RECOVERY_RW_DEV_MISMATCH 0x45
/* Shared data error in rewritable firmware */
#define VBNV_RECOVERY_RW_SHARED_DATA  0x46
/* Test error from LoadKernel() */
#define VBNV_RECOVERY_RW_TEST_LK      0x47
/* No bootable disk found */
#define VBNV_RECOVERY_RW_NO_DISK      0x48
/* Unspecified/unknown error in rewritable firmware */
#define VBNV_RECOVERY_RW_UNSPECIFIED  0x7F
/* DM-verity error */
#define VBNV_RECOVERY_KE_DM_VERITY    0x81
/* Unspecified/unknown error in kernel */
#define VBNV_RECOVERY_KE_UNSPECIFIED  0xBF
/* Recovery mode test from user-mode */
#define VBNV_RECOVERY_US_TEST         0xC1
/* Unspecified/unknown error in user-mode */
#define VBNV_RECOVERY_US_UNSPECIFIED  0xFF


/* Initialize the NV storage library.  This must be called before any
 * other functions in this library.  Returns 0 if success, non-zero if
 * error.
 *
 * Proper calling procedure:
 *    1) Allocate a context struct.
 *    2) If multi-threaded/multi-process, acquire a lock to prevent
 *       other processes from modifying the underlying storage.
 *    3) Read underlying storage and fill in context->raw.
 *    4) Call VbNvSetup().
 *
 * If you have access to global variables, you may want to wrap all
 * that in your own VbNvOpen() function.  We don't do that in here
 * because there are no global variables in UEFI BIOS during the PEI
 * phase (that's also why we have to pass around a context pointer). */
int VbNvSetup(VbNvContext* context);

/* Clean up and flush changes back to the raw data.  This must be
 * called after other functions in this library.  Returns 0 if
 * success, non-zero if error.
 *
 * Proper calling procedure:
 *    1) Call VbNvExit().
 *    2) If context.raw_changed, write data back to underlying storage.
 *    3) Release any lock you acquired before calling VbNvSetup().
 *    4) Free the context struct.
 *
 * If you have access to global variables, you may want to wrap this
 * in your own VbNvClose() function. */
int VbNvTeardown(VbNvContext* context);

/* Read a NV storage parameter into *dest.  Returns 0 if success,
 * non-zero if error.
 *
 * This may only be called between VbNvSetup() and VbNvTeardown(). */
int VbNvGet(VbNvContext* context, VbNvParam param, uint32_t* dest);

/* Set a NV storage param to a new value.  Returns 0 if success,
 * non-zero if error.
 *
 * This may only be called between VbNvSetup() and VbNvTeardown(). */
int VbNvSet(VbNvContext* context, VbNvParam param, uint32_t value);


#endif  /* VBOOT_REFERENCE_NVSTORAGE_H_ */
