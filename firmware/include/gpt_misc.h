/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef VBOOT_REFERENCE_CGPT_MISC_H_
#define VBOOT_REFERENCE_CGPT_MISC_H_

#include "vboot_api.h"

enum {
	GPT_SUCCESS = 0,
	GPT_ERROR_NO_VALID_KERNEL,
	GPT_ERROR_INVALID_HEADERS,
	GPT_ERROR_INVALID_ENTRIES,
	GPT_ERROR_INVALID_SECTOR_SIZE,
	GPT_ERROR_INVALID_SECTOR_NUMBER,
	GPT_ERROR_INVALID_UPDATE_TYPE,
	GPT_ERROR_CRC_CORRUPTED,
	GPT_ERROR_OUT_OF_REGION,
	GPT_ERROR_START_LBA_OVERLAP,
	GPT_ERROR_END_LBA_OVERLAP,
	GPT_ERROR_DUP_GUID,
	GPT_ERROR_INVALID_FLASH_GEOMETRY,
	GPT_ERROR_NO_SUCH_ENTRY,
	/* Number of errors */
	GPT_ERROR_COUNT
};

/* Bit masks for GptData.modified field. */
#define GPT_MODIFIED_HEADER1 0x01
#define GPT_MODIFIED_HEADER2 0x02
#define GPT_MODIFIED_ENTRIES1 0x04
#define GPT_MODIFIED_ENTRIES2 0x08

/*
 * Size of GptData.primary_entries and secondary_entries: 128 bytes/entry * 128
 * entries.
 */
#define TOTAL_ENTRIES_SIZE 16384

/*
 * The 'update_type' of GptUpdateKernelEntry().  We expose TRY and BAD only
 * because those are what verified boot needs.  For more precise control on GPT
 * attribute bits, please refer to gpt_internal.h.
 */
enum {
	/*
	 * System will be trying to boot the currently selected kernel
	 * partition.  Update its try count if necessary.
	 */
	GPT_UPDATE_ENTRY_TRY = 1,
	/*
	 * The currently selected kernel partition failed validation.  Mark
	 * entry as invalid.
	 */
	GPT_UPDATE_ENTRY_BAD = 2,
};

/* If this bit is 1, the GPT is stored in another from the streaming data */
#define GPT_FLAG_EXTERNAL	0x1

/*
 * A note about stored_on_device and gpt_drive_sectors:
 *
 * This code is used by both the "cgpt" utility and depthcharge/vboot. ATM,
 * depthcharge does not have logic to properly setup stored_on_device and
 * gpt_drive_sectors, but it does do a memset(gpt, 0, sizeof(GptData)). And so,
 * GPT_STORED_ON_DEVICE should be 0 to make stored_on_device compatible with
 * present behavior. At the same time, in vboot_kernel:LoadKernel(), and
 * cgpt_common:GptLoad(), we need to have simple shims to set gpt_drive_sectors
 * to drive_sectors.
 *
 * TODO(namnguyen): Remove those shims when the firmware can set these fields.
 */
typedef struct {
	/* Fill in the following fields before calling GptInit() */
	/* GPT primary header, from sector 1 of disk (size: 512 bytes) */
	uint8_t *primary_header;
	/* GPT secondary header, from last sector of disk (size: 512 bytes) */
	uint8_t *secondary_header;
	/* Primary GPT table, follows primary header (size: 16 KB) */
	uint8_t *primary_entries;
	/* Secondary GPT table, precedes secondary header (size: 16 KB) */
	uint8_t *secondary_entries;
	/* Size of a LBA sector, in bytes */
	uint32_t sector_bytes;
	/* Size of drive (that the partitions are on) in LBA sectors */
	uint64_t streaming_drive_sectors;
	/* Size of the device that holds the GPT structures, 512-byte sectors */
	uint64_t gpt_drive_sectors;
	/* Flags */
	uint32_t flags;

	/* Outputs */
	/* Which inputs have been modified?  GPT_MODIFIED_* */
	uint8_t modified;
	/*
	 * The current chromeos kernel index in partition table.  -1 means not
	 * found on drive. Note that GPT partition numbers are traditionally
	 * 1-based, but we're using a zero-based index here.
	 */
	int current_kernel;

	/* Internal variables */
	uint32_t valid_headers, valid_entries;
	int current_priority;
} GptData;

/**
 * Initializes the GPT data structure's internal state.
 *
 * The following fields must be filled before calling this function:
 *
 *   primary_header
 *   secondary_header
 *   primary_entries
 *   secondary_entries
 *   sector_bytes
 *   drive_sectors
 *   stored_on_device
 *   gpt_device_sectors
 *
 * On return the modified field may be set, if the GPT data has been modified
 * and should be written to disk.
 *
 * Returns GPT_SUCCESS if successful, non-zero if error:
 *   GPT_ERROR_INVALID_HEADERS, both partition table headers are invalid, enters
 *                              recovery mode,
 *   GPT_ERROR_INVALID_ENTRIES, both partition table entries are invalid, enters
 *                              recovery mode,
 *   GPT_ERROR_INVALID_SECTOR_SIZE, size of a sector is not supported,
 *   GPT_ERROR_INVALID_SECTOR_NUMBER, number of sectors in drive is invalid (too
 *                                    small) */
int GptInit(GptData *gpt);

/**
 * Allocate and read GPT data from the drive.  The sector_bytes and
 * drive_sectors fields should be filled on input.  The primary and secondary
 * header and entries are filled on output.
 *
 * Returns 0 if successful, 1 if error.
 */
int AllocAndReadGptData(VbExDiskHandle_t disk_handle, GptData *gptdata);

/**
 * Write any changes for the GPT data back to the drive, then free the buffers.
 */
int WriteAndFreeGptData(VbExDiskHandle_t disk_handle, GptData *gptdata);

#endif  /* VBOOT_REFERENCE_CGPT_MISC_H_ */
