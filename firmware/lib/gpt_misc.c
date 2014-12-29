/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "sysincludes.h"

#include "cgptlib.h"
#include "cgptlib_internal.h"
#include "crc32.h"
#include "gpt.h"
#include "utility.h"
#include "vboot_api.h"


/**
 * Allocate and read GPT data from the drive.
 *
 * The sector_bytes and drive_sectors fields should be filled on input.  The
 * primary and secondary header and entries are filled on output.
 *
 * Returns 0 if successful, 1 if error.
 */
int AllocAndReadGptData(VbExDiskHandle_t disk_handle, GptData *gptdata)
{
	uint64_t entries_sectors = TOTAL_ENTRIES_SIZE / gptdata->sector_bytes;
	int primary_valid = 0, secondary_valid = 0;

	/* No data to be written yet */
	gptdata->modified = 0;

	/* Allocate all buffers */
	gptdata->primary_header = (uint8_t *)VbExMalloc(gptdata->sector_bytes);
	gptdata->secondary_header =
		(uint8_t *)VbExMalloc(gptdata->sector_bytes);
	gptdata->primary_entries = (uint8_t *)VbExMalloc(TOTAL_ENTRIES_SIZE);
	gptdata->secondary_entries = (uint8_t *)VbExMalloc(TOTAL_ENTRIES_SIZE);

	if (gptdata->primary_header == NULL ||
	    gptdata->secondary_header == NULL ||
	    gptdata->primary_entries == NULL ||
	    gptdata->secondary_entries == NULL)
		return 1;

	/* Read primary header from the drive, skipping the protective MBR */
	if (0 != VbExDiskRead(disk_handle, 1, 1, gptdata->primary_header))
		return 1;

	/* Only read primary GPT if the primary header is valid */
	GptHeader* primary_header = (GptHeader*)gptdata->primary_header;
	if (0 == CheckHeader(primary_header, 0, gptdata->drive_sectors)) {
		primary_valid = 1;
		if (0 != VbExDiskRead(disk_handle,
				      primary_header->entries_lba,
				      entries_sectors,
				      gptdata->primary_entries))
			return 1;
	} else {
		VBDEBUG(("Primary GPT header invalid!\n"));
	}

	/* Read secondary header from the end of the drive */
	if (0 != VbExDiskRead(disk_handle, gptdata->drive_sectors - 1, 1,
			      gptdata->secondary_header))
		return 1;

	/* Only read secondary GPT if the secondary header is valid */
	GptHeader* secondary_header = (GptHeader*)gptdata->secondary_header;
	if (0 == CheckHeader(secondary_header, 1, gptdata->drive_sectors)) {
		secondary_valid = 1;
		if (0 != VbExDiskRead(disk_handle,
				      secondary_header->entries_lba,
				      entries_sectors,
				      gptdata->secondary_entries))
			return 1;
	} else {
		VBDEBUG(("Secondary GPT header invalid!\n"));
	}

	/* Return 0 if least one GPT header was valid */
	return (primary_valid || secondary_valid) ? 0 : 1;
}

/**
 * Write any changes for the GPT data back to the drive, then free the buffers.
 *
 * Returns 0 if successful, 1 if error.
 */
int WriteAndFreeGptData(VbExDiskHandle_t disk_handle, GptData *gptdata)
{
	int legacy = 0;
	uint64_t entries_sectors = TOTAL_ENTRIES_SIZE / gptdata->sector_bytes;
	int ret = 1;

	/*
	 * TODO(namnguyen): Preserve padding between primary GPT header and
	 * its entries.
	 */
	uint64_t entries_lba = GPT_PMBR_SECTORS + GPT_HEADER_SECTORS;
	if (gptdata->primary_header) {
		GptHeader *h = (GptHeader *)(gptdata->primary_header);
		entries_lba = h->entries_lba;

		/*
		 * Avoid even looking at this data if we don't need to. We
		 * may in fact not have read it from disk if the read failed,
		 * and this avoids a valgrind complaint.
		 */
		if (gptdata->modified) {
			legacy = !Memcmp(h->signature, GPT_HEADER_SIGNATURE2,
					GPT_HEADER_SIGNATURE_SIZE);
		}
		if (gptdata->modified & GPT_MODIFIED_HEADER1) {
			if (legacy) {
				VBDEBUG(("Not updating GPT header 1: "
					 "legacy mode is enabled.\n"));
			} else {
				VBDEBUG(("Updating GPT header 1\n"));
				if (0 != VbExDiskWrite(disk_handle, 1, 1,
						       gptdata->primary_header))
					goto fail;
			}
		}
	}

	if (gptdata->primary_entries) {
		if (gptdata->modified & GPT_MODIFIED_ENTRIES1) {
			if (legacy) {
				VBDEBUG(("Not updating GPT entries 1: "
					 "legacy mode is enabled.\n"));
			} else {
				VBDEBUG(("Updating GPT entries 1\n"));
				if (0 != VbExDiskWrite(disk_handle, entries_lba,
						entries_sectors,
						gptdata->primary_entries))
					goto fail;
			}
		}
	}

	entries_lba = (gptdata->drive_sectors - entries_sectors -
		GPT_HEADER_SECTORS);
	if (gptdata->secondary_header) {
		GptHeader *h = (GptHeader *)(gptdata->secondary_header);
		entries_lba = h->entries_lba;
		if (gptdata->modified & GPT_MODIFIED_HEADER2) {
			VBDEBUG(("Updating GPT entries 2\n"));
			if (0 != VbExDiskWrite(disk_handle,
					       gptdata->drive_sectors - 1, 1,
					       gptdata->secondary_header))
				goto fail;
		}
	}

	if (gptdata->secondary_entries) {
		if (gptdata->modified & GPT_MODIFIED_ENTRIES2) {
			VBDEBUG(("Updating GPT header 2\n"));
			if (0 != VbExDiskWrite(disk_handle,
				entries_lba, entries_sectors,
				gptdata->secondary_entries))
				goto fail;
		}
	}

	ret = 0;

fail:
	/* Avoid leaking memory on disk write failure */
	if (gptdata->primary_header)
		VbExFree(gptdata->primary_header);
	if (gptdata->primary_entries)
		VbExFree(gptdata->primary_entries);
	if (gptdata->secondary_entries)
		VbExFree(gptdata->secondary_entries);
	if (gptdata->secondary_header)
		VbExFree(gptdata->secondary_header);

	/* Success */
	return ret;
}

