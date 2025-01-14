/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for misc library
 */

#include "2sysincludes.h"
#include "2api.h"
#include "2common.h"
#include "2misc.h"
#include "2nvstorage.h"
#include "2secdata.h"

#include "test_common.h"

/* Common context for tests */
static uint8_t workbuf[VB2_FIRMWARE_WORKBUF_RECOMMENDED_SIZE]
	__attribute__ ((aligned (VB2_WORKBUF_ALIGN)));
static struct vb2_context ctx;
static struct vb2_shared_data *sd;
static struct vb2_gbb_header gbb;

/* Mocked function data */
enum vb2_resource_index mock_resource_index;
void *mock_resource_ptr;
uint32_t mock_resource_size;
int mock_tpm_clear_called;
int mock_tpm_clear_retval;


static void reset_common_data(void)
{
	memset(workbuf, 0xaa, sizeof(workbuf));

	memset(&ctx, 0, sizeof(ctx));
	ctx.workbuf = workbuf;
	ctx.workbuf_size = sizeof(workbuf);

	vb2_init_context(&ctx);
	sd = vb2_get_sd(&ctx);

	memset(&gbb, 0, sizeof(gbb));

	vb2_nv_init(&ctx);

	vb2api_secdata_create(&ctx);
	vb2_secdata_init(&ctx);

	mock_tpm_clear_called = 0;
	mock_tpm_clear_retval = VB2_SUCCESS;
};

/* Mocked functions */
struct vb2_gbb_header *vb2_get_gbb(struct vb2_context *c)
{
	return &gbb;
}

int vb2ex_read_resource(struct vb2_context *c,
			enum vb2_resource_index index,
			uint32_t offset,
			void *buf,
			uint32_t size)
{
	if (index != mock_resource_index)
		return VB2_ERROR_EX_READ_RESOURCE_INDEX;

	if (offset > mock_resource_size || offset + size > mock_resource_size)
		return VB2_ERROR_EX_READ_RESOURCE_SIZE;

	memcpy(buf, (uint8_t *)mock_resource_ptr + offset, size);
	return VB2_SUCCESS;
}

int vb2ex_tpm_clear_owner(struct vb2_context *c)
{
	mock_tpm_clear_called++;

	return mock_tpm_clear_retval;
}

/* Tests */
static void init_context_tests(void)
{
	/* Use our own context struct so we can re-init it */
	struct vb2_context c = {
		.workbuf = workbuf,
		.workbuf_size = sizeof(workbuf),
	};

	reset_common_data();

	TEST_SUCC(vb2_init_context(&c), "Init context good");
	TEST_EQ(c.workbuf_used, vb2_wb_round_up(sizeof(struct vb2_shared_data)),
		"Init vbsd");
	TEST_EQ(sd->magic, VB2_SHARED_DATA_MAGIC, "Bad magic");
	TEST_EQ(sd->struct_version_major, VB2_SHARED_DATA_VERSION_MAJOR,
		"No major version");
	TEST_EQ(sd->struct_version_minor, VB2_SHARED_DATA_VERSION_MINOR,
		"No minor version");

	/* Don't re-init if used is non-zero */
	c.workbuf_used = 200;
	TEST_SUCC(vb2_init_context(&c), "Re-init context good");
	TEST_EQ(c.workbuf_used, 200, "Didn't re-init");

	/* Error if re-init with incorrect magic */
	sd->magic = 0xdeadbeef;
	TEST_EQ(vb2_init_context(&c),
		VB2_ERROR_SHARED_DATA_MAGIC, "Missed bad magic");
	sd->magic = VB2_SHARED_DATA_MAGIC;

	/* Success if re-init with higher minor version */
	sd->struct_version_minor++;
	TEST_SUCC(vb2_init_context(&c), "Didn't allow higher minor version");
	sd->struct_version_minor = VB2_SHARED_DATA_VERSION_MINOR;

	/* Error if re-init with lower minor version */
	if (VB2_SHARED_DATA_VERSION_MINOR > 0) {
		sd->struct_version_minor--;
		TEST_EQ(vb2_init_context(&c), VB2_ERROR_SHARED_DATA_VERSION,
			"Allowed lower minor version");
		sd->struct_version_minor = VB2_SHARED_DATA_VERSION_MINOR;
	}

	/* Error if re-init with higher major version */
	sd->struct_version_major++;
	TEST_EQ(vb2_init_context(&c),
		VB2_ERROR_SHARED_DATA_VERSION, "Allowed higher major version");
	sd->struct_version_major = VB2_SHARED_DATA_VERSION_MAJOR;

	/* Error if re-init with lower major version */
	sd->struct_version_major--;
	TEST_EQ(vb2_init_context(&c),
		VB2_ERROR_SHARED_DATA_VERSION, "Allowed lower major version");
	sd->struct_version_major = VB2_SHARED_DATA_VERSION_MAJOR;

	/* Handle workbuf errors */
	c.workbuf_used = 0;
	c.workbuf_size = sizeof(struct vb2_shared_data) - 1;
	TEST_EQ(vb2_init_context(&c),
		VB2_ERROR_INITCTX_WORKBUF_SMALL, "Init too small");
	c.workbuf_size = sizeof(workbuf);

	/* Handle workbuf unaligned */
	c.workbuf++;
	TEST_EQ(vb2_init_context(&c),
		VB2_ERROR_INITCTX_WORKBUF_ALIGN, "Init unaligned");
}

static void misc_tests(void)
{
	struct vb2_workbuf wb;

	reset_common_data();
	ctx.workbuf_used = VB2_WORKBUF_ALIGN;

	vb2_workbuf_from_ctx(&ctx, &wb);

	TEST_PTR_EQ(wb.buf, workbuf + VB2_WORKBUF_ALIGN,
		    "vb_workbuf_from_ctx() buf");
	TEST_EQ(wb.size, ctx.workbuf_size - VB2_WORKBUF_ALIGN,
		"vb_workbuf_from_ctx() size");
}

static void gbb_tests(void)
{
	struct vb2_gbb_header gbbsrc = {
		.signature = {'$', 'G', 'B', 'B'},
		.major_version = VB2_GBB_MAJOR_VER,
		.minor_version = VB2_GBB_MINOR_VER,
		.header_size = sizeof(struct vb2_gbb_header),
		.flags = 0x1234,
		.rootkey_offset = 240,
		.rootkey_size = 1040,
	};

	struct vb2_gbb_header gbbdest;

	TEST_EQ(sizeof(struct vb2_gbb_header),
		EXPECTED_VB2_GBB_HEADER_SIZE,
		"sizeof(struct vb2_gbb_header)");

	reset_common_data();

	/* Good contents */
	mock_resource_index = VB2_RES_GBB;
	mock_resource_ptr = &gbbsrc;
	mock_resource_size = sizeof(gbbsrc);
	TEST_SUCC(vb2_read_gbb_header(&ctx, &gbbdest), "read gbb header good");
	TEST_SUCC(memcmp(&gbbsrc, &gbbdest, sizeof(gbbsrc)),
		  "read gbb contents");

	mock_resource_index = VB2_RES_GBB + 1;
	TEST_EQ(vb2_read_gbb_header(&ctx, &gbbdest),
		VB2_ERROR_EX_READ_RESOURCE_INDEX, "read gbb header missing");
	mock_resource_index = VB2_RES_GBB;

	gbbsrc.signature[0]++;
	TEST_EQ(vb2_read_gbb_header(&ctx, &gbbdest),
		VB2_ERROR_GBB_MAGIC, "read gbb header bad magic");
	gbbsrc.signature[0]--;

	gbbsrc.major_version = VB2_GBB_MAJOR_VER + 1;
	TEST_EQ(vb2_read_gbb_header(&ctx, &gbbdest),
		VB2_ERROR_GBB_VERSION, "read gbb header major version");
	gbbsrc.major_version = VB2_GBB_MAJOR_VER;

	gbbsrc.minor_version = VB2_GBB_MINOR_VER + 1;
	TEST_SUCC(vb2_read_gbb_header(&ctx, &gbbdest),
		  "read gbb header minor++");
	gbbsrc.minor_version = 1;
	TEST_EQ(vb2_read_gbb_header(&ctx, &gbbdest),
		VB2_ERROR_GBB_TOO_OLD, "read gbb header 1.1 fails");
	gbbsrc.minor_version = 0;
	TEST_EQ(vb2_read_gbb_header(&ctx, &gbbdest),
		VB2_ERROR_GBB_TOO_OLD, "read gbb header 1.0 fails");
	gbbsrc.minor_version = VB2_GBB_MINOR_VER;

	gbbsrc.header_size--;
	TEST_EQ(vb2_read_gbb_header(&ctx, &gbbdest),
		VB2_ERROR_GBB_HEADER_SIZE, "read gbb header size");
	TEST_EQ(vb2_fw_parse_gbb(&ctx),
		VB2_ERROR_GBB_HEADER_SIZE, "parse gbb failure");
	gbbsrc.header_size++;

	/* Parse GBB */
	int used_before = ctx.workbuf_used;
	TEST_SUCC(vb2_fw_parse_gbb(&ctx), "parse gbb");
	/* Manually calculate the location of GBB since we have mocked out the
	   original definition of vb2_get_gbb. */
	struct vb2_gbb_header *current_gbb =
		(struct vb2_gbb_header *)((void *)sd + sd->gbb_offset);
	TEST_SUCC(memcmp(&gbbsrc, current_gbb, sizeof(gbbsrc)),
		  "copy gbb contents");
	TEST_EQ(used_before, ctx.workbuf_used - sizeof(gbbsrc),
		"unexpected workbuf size");

	/* Workbuf failure */
	reset_common_data();
	ctx.workbuf_used = ctx.workbuf_size - 4;
	TEST_EQ(vb2_fw_parse_gbb(&ctx),
		VB2_ERROR_GBB_WORKBUF, "parse gbb no workbuf");
}

static void fail_tests(void)
{
	/* Early fail (before even NV init) */
	reset_common_data();
	sd->status &= ~VB2_SD_STATUS_NV_INIT;
	vb2_fail(&ctx, 1, 2);
	TEST_NEQ(sd->status & VB2_SD_STATUS_NV_INIT, 0, "vb2_fail inits NV");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST),
		1, "vb2_fail request");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_SUBCODE),
		2, "vb2_fail subcode");

	/* Repeated fail doesn't overwrite the error code */
	vb2_fail(&ctx, 3, 4);
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST),
		1, "vb2_fail repeat");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_SUBCODE),
		2, "vb2_fail repeat2");

	/* Fail with other slot good doesn't trigger recovery */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_TRY_COUNT, 3);
	vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_UNKNOWN);
	sd->status |= VB2_SD_STATUS_CHOSE_SLOT;
	sd->fw_slot = 0;
	sd->last_fw_slot = 1;
	sd->last_fw_result = VB2_FW_RESULT_UNKNOWN;
	vb2_fail(&ctx, 5, 6);
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST), 0, "vb2_failover");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_RESULT),
		VB2_FW_RESULT_FAILURE, "vb2_fail this fw");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_COUNT), 0, "vb2_fail use up tries");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_NEXT), 1, "vb2_fail try other slot");

	/* Fail with other slot already failing triggers recovery */
	reset_common_data();
	sd->status |= VB2_SD_STATUS_CHOSE_SLOT;
	sd->fw_slot = 1;
	sd->last_fw_slot = 0;
	sd->last_fw_result = VB2_FW_RESULT_FAILURE;
	vb2_fail(&ctx, 7, 8);
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST), 7,
		"vb2_fail both slots bad");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_RESULT),
		VB2_FW_RESULT_FAILURE, "vb2_fail this fw");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_NEXT), 0, "vb2_fail try other slot");
}

static void recovery_tests(void)
{
	/* No recovery */
	reset_common_data();
	vb2_check_recovery(&ctx);
	TEST_EQ(sd->recovery_reason, 0, "No recovery reason");
	TEST_EQ(sd->flags & VB2_SD_FLAG_MANUAL_RECOVERY,
		0, "Not manual recovery");
	TEST_EQ(ctx.flags & VB2_CONTEXT_RECOVERY_MODE,
		0, "Not recovery mode");

	/* From request */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_RECOVERY_REQUEST, 3);
	vb2_check_recovery(&ctx);
	TEST_EQ(sd->recovery_reason, 3, "Recovery reason from request");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST), 0, "NV cleared");
	TEST_EQ(sd->flags & VB2_SD_FLAG_MANUAL_RECOVERY,
		0, "Not manual recovery");
	TEST_NEQ(ctx.flags & VB2_CONTEXT_RECOVERY_MODE,
		 0, "Recovery mode");

	/* From request, but already failed */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_RECOVERY_REQUEST, 4);
	sd->recovery_reason = 5;
	vb2_check_recovery(&ctx);
	TEST_EQ(sd->recovery_reason, 5, "Recovery reason already failed");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST),
		0, "NV still cleared");

	/* Override */
	reset_common_data();
	sd->recovery_reason = 6;
	ctx.flags |= VB2_CONTEXT_FORCE_RECOVERY_MODE;
	vb2_check_recovery(&ctx);
	TEST_EQ(sd->recovery_reason, VB2_RECOVERY_RO_MANUAL,
		"Recovery reason forced");
	TEST_NEQ(sd->flags & VB2_SD_FLAG_MANUAL_RECOVERY,
		 0, "SD flag set");

	/* Override at broken screen */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_RECOVERY_SUBCODE, VB2_RECOVERY_US_TEST);
	ctx.flags |= VB2_CONTEXT_FORCE_RECOVERY_MODE;
	vb2_check_recovery(&ctx);
	TEST_EQ(sd->recovery_reason, VB2_RECOVERY_US_TEST,
		"Recovery reason forced from broken");
	TEST_NEQ(sd->flags & VB2_SD_FLAG_MANUAL_RECOVERY,
		 0, "SD flag set");
}

static void dev_switch_tests(void)
{
	uint32_t v;

	/* Normal mode */
	reset_common_data();
	TEST_SUCC(vb2_check_dev_switch(&ctx), "dev mode off");
	TEST_EQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd not in dev");
	TEST_EQ(ctx.flags & VB2_CONTEXT_DEVELOPER_MODE, 0, "  ctx not in dev");
	TEST_EQ(mock_tpm_clear_called, 0, "  no tpm clear");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_REQ_WIPEOUT), 0, "  no nv wipeout");

	/* Dev mode */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS,
			(VB2_SECDATA_FLAG_DEV_MODE |
			 VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER));
	TEST_SUCC(vb2_check_dev_switch(&ctx), "dev mode on");
	TEST_NEQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd in dev");
	TEST_NEQ(ctx.flags & VB2_CONTEXT_DEVELOPER_MODE, 0, "  ctx in dev");
	TEST_EQ(mock_tpm_clear_called, 0, "  no tpm clear");

	/* Any normal mode boot clears dev boot flags */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_DEV_BOOT_USB, 1);
	vb2_nv_set(&ctx, VB2_NV_DEV_BOOT_LEGACY, 1);
	vb2_nv_set(&ctx, VB2_NV_DEV_BOOT_SIGNED_ONLY, 1);
	vb2_nv_set(&ctx, VB2_NV_DEV_BOOT_FASTBOOT_FULL_CAP, 1);
	vb2_nv_set(&ctx, VB2_NV_DEV_DEFAULT_BOOT, 1);
	vb2_nv_set(&ctx, VB2_NV_FASTBOOT_UNLOCK_IN_FW, 1);
	TEST_SUCC(vb2_check_dev_switch(&ctx), "dev mode off");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DEV_BOOT_USB),
		0, "  cleared dev boot usb");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DEV_BOOT_LEGACY),
		0, "  cleared dev boot legacy");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DEV_BOOT_SIGNED_ONLY),
		0, "  cleared dev boot signed only");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DEV_BOOT_FASTBOOT_FULL_CAP),
		0, "  cleared dev boot fastboot full cap");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DEV_DEFAULT_BOOT),
		0, "  cleared dev default boot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FASTBOOT_UNLOCK_IN_FW),
		0, "  cleared dev boot fastboot unlock in fw");

	/* Normal-dev transition clears TPM */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS, VB2_SECDATA_FLAG_DEV_MODE);
	TEST_SUCC(vb2_check_dev_switch(&ctx), "to dev mode");
	TEST_EQ(mock_tpm_clear_called, 1, "  tpm clear");
	vb2_secdata_get(&ctx, VB2_SECDATA_FLAGS, &v);
	TEST_EQ(v, (VB2_SECDATA_FLAG_DEV_MODE |
		    VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER),
		"  last boot developer now");

	/* Dev-normal transition clears TPM too */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS,
			VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER);
	TEST_SUCC(vb2_check_dev_switch(&ctx), "from dev mode");
	TEST_EQ(mock_tpm_clear_called, 1, "  tpm clear");
	vb2_secdata_get(&ctx, VB2_SECDATA_FLAGS, &v);
	TEST_EQ(v, 0, "  last boot not developer now");

	/* Disable dev mode */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS,
			(VB2_SECDATA_FLAG_DEV_MODE |
			 VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER));
	vb2_nv_set(&ctx, VB2_NV_DISABLE_DEV_REQUEST, 1);
	TEST_SUCC(vb2_check_dev_switch(&ctx), "disable dev request");
	TEST_EQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd not in dev");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DISABLE_DEV_REQUEST),
		0, "  request cleared");

	/* Force enabled by GBB */
	reset_common_data();
	gbb.flags |= VB2_GBB_FLAG_FORCE_DEV_SWITCH_ON;
	TEST_SUCC(vb2_check_dev_switch(&ctx), "dev on via gbb");
	TEST_NEQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd in dev");
	vb2_secdata_get(&ctx, VB2_SECDATA_FLAGS, &v);
	TEST_EQ(v, VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER,
		"  doesn't set dev on in secdata but does set last boot dev");
	TEST_EQ(mock_tpm_clear_called, 1, "  tpm clear");

	/* Request disable by ctx flag */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS,
			(VB2_SECDATA_FLAG_DEV_MODE |
			 VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER));
	ctx.flags |= VB2_CONTEXT_DISABLE_DEVELOPER_MODE;
	TEST_SUCC(vb2_check_dev_switch(&ctx), "disable dev on ctx request");
	TEST_EQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd not in dev");

	/* Simulate clear owner failure */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS,
			VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER);
	mock_tpm_clear_retval = VB2_ERROR_EX_TPM_CLEAR_OWNER;
	TEST_EQ(vb2_check_dev_switch(&ctx),
		VB2_ERROR_EX_TPM_CLEAR_OWNER, "tpm clear fail");
	TEST_EQ(mock_tpm_clear_called, 1, "  tpm clear");
	vb2_secdata_get(&ctx, VB2_SECDATA_FLAGS, &v);
	TEST_EQ(v, VB2_SECDATA_FLAG_LAST_BOOT_DEVELOPER,
		"  last boot still developer");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_REQUEST),
		VB2_RECOVERY_TPM_CLEAR_OWNER, "  requests recovery");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_RECOVERY_SUBCODE),
		(uint8_t)VB2_ERROR_EX_TPM_CLEAR_OWNER, "  recovery subcode");

	/*
	 * Secdata failure in normal mode fails and shows dev=0 even if dev
	 * mode was on in the (inaccessible) secdata.
	 */
	reset_common_data();
	vb2_secdata_set(&ctx, VB2_SECDATA_FLAGS, VB2_SECDATA_FLAG_DEV_MODE);
	sd->status &= ~VB2_SD_STATUS_SECDATA_INIT;
	TEST_EQ(vb2_check_dev_switch(&ctx), VB2_ERROR_SECDATA_GET_UNINITIALIZED,
		"secdata fail normal");
	TEST_EQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd not in dev");
	TEST_EQ(ctx.flags & VB2_CONTEXT_DEVELOPER_MODE, 0, "  ctx not in dev");

	/* Secdata failure in recovery mode continues */
	reset_common_data();
	ctx.flags |= VB2_CONTEXT_RECOVERY_MODE;
	sd->status &= ~VB2_SD_STATUS_SECDATA_INIT;
	TEST_SUCC(vb2_check_dev_switch(&ctx), "secdata fail recovery");
	TEST_EQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd not in dev");
	TEST_EQ(ctx.flags & VB2_CONTEXT_DEVELOPER_MODE, 0, "  ctx not in dev");

	/* And doesn't check or clear dev disable request */
	reset_common_data();
	ctx.flags |= VB2_CONTEXT_RECOVERY_MODE;
	sd->status &= ~VB2_SD_STATUS_SECDATA_INIT;
	vb2_nv_set(&ctx, VB2_NV_DISABLE_DEV_REQUEST, 1);
	TEST_SUCC(vb2_check_dev_switch(&ctx), "secdata fail recovery disable");
	TEST_EQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd not in dev");
	TEST_EQ(ctx.flags & VB2_CONTEXT_DEVELOPER_MODE, 0, "  ctx not in dev");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_DISABLE_DEV_REQUEST),
		1, "  request not cleared");

	/* Can still override with GBB flag */
	reset_common_data();
	ctx.flags |= VB2_CONTEXT_RECOVERY_MODE;
	sd->status &= ~VB2_SD_STATUS_SECDATA_INIT;
	gbb.flags |= VB2_GBB_FLAG_FORCE_DEV_SWITCH_ON;
	TEST_SUCC(vb2_check_dev_switch(&ctx), "secdata fail recovery gbb");
	TEST_NEQ(sd->flags & VB2_SD_FLAG_DEV_MODE_ENABLED, 0, "  sd in dev");
	TEST_NEQ(ctx.flags & VB2_CONTEXT_DEVELOPER_MODE, 0, "  ctx in dev");
	TEST_EQ(mock_tpm_clear_called, 1, "  tpm clear");

	/* Force wipeout by ctx flag */
	reset_common_data();
	ctx.flags |= VB2_CONTEXT_FORCE_WIPEOUT_MODE;
	TEST_SUCC(vb2_check_dev_switch(&ctx), "wipeout on ctx flag");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_REQ_WIPEOUT), 1, "  nv wipeout");
}

static void tpm_clear_tests(void)
{
	/* No clear request */
	reset_common_data();
	TEST_SUCC(vb2_check_tpm_clear(&ctx), "no clear request");
	TEST_EQ(mock_tpm_clear_called, 0, "tpm not cleared");

	/* Successful request */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_CLEAR_TPM_OWNER_REQUEST, 1);
	TEST_SUCC(vb2_check_tpm_clear(&ctx), "clear request");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_CLEAR_TPM_OWNER_REQUEST),
		0, "request cleared");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_CLEAR_TPM_OWNER_DONE),
		1, "done set");
	TEST_EQ(mock_tpm_clear_called, 1, "tpm cleared");

	/* Failed request */
	reset_common_data();
	mock_tpm_clear_retval = VB2_ERROR_EX_TPM_CLEAR_OWNER;
	vb2_nv_set(&ctx, VB2_NV_CLEAR_TPM_OWNER_REQUEST, 1);
	TEST_EQ(vb2_check_tpm_clear(&ctx),
		VB2_ERROR_EX_TPM_CLEAR_OWNER, "clear failure");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_CLEAR_TPM_OWNER_REQUEST),
		0, "request cleared");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_CLEAR_TPM_OWNER_DONE),
		0, "done not set");
}

static void select_slot_tests(void)
{
	/* Slot A */
	reset_common_data();
	TEST_SUCC(vb2_select_fw_slot(&ctx), "select slot A");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_RESULT),
		VB2_FW_RESULT_UNKNOWN, "result unknown");
	TEST_NEQ(sd->status & VB2_SD_STATUS_CHOSE_SLOT, 0, "chose slot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_TRIED), 0, "tried A");
	TEST_EQ(sd->fw_slot, 0, "selected A");
	TEST_EQ(ctx.flags & VB2_CONTEXT_FW_SLOT_B, 0, "didn't choose B");

	/* Slot B */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_TRY_NEXT, 1);
	TEST_SUCC(vb2_select_fw_slot(&ctx), "select slot B");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_RESULT),
		VB2_FW_RESULT_UNKNOWN, "result unknown");
	TEST_NEQ(sd->status & VB2_SD_STATUS_CHOSE_SLOT, 0, "chose slot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_TRIED), 1, "tried B");
	TEST_EQ(sd->fw_slot, 1, "selected B");
	TEST_NEQ(ctx.flags & VB2_CONTEXT_FW_SLOT_B, 0, "ctx says choose B");

	/* Slot A ran out of tries */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_TRYING);
	TEST_SUCC(vb2_select_fw_slot(&ctx), "select slot A out of tries");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_NEXT), 1, "try B next");
	TEST_NEQ(sd->status & VB2_SD_STATUS_CHOSE_SLOT, 0, "chose slot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_TRIED), 1, "tried B");
	TEST_EQ(sd->fw_slot, 1, "selected B");
	TEST_NEQ(ctx.flags & VB2_CONTEXT_FW_SLOT_B, 0, "ctx says choose B");

	/* Slot A ran out of tries, even with nofail active */
	reset_common_data();
	ctx.flags |= VB2_CONTEXT_NOFAIL_BOOT;
	vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_TRYING);
	TEST_SUCC(vb2_select_fw_slot(&ctx), "select slot A out of tries");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_NEXT), 1, "try B next");
	TEST_NEQ(sd->status & VB2_SD_STATUS_CHOSE_SLOT, 0, "chose slot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_TRIED), 1, "tried B");
	TEST_EQ(sd->fw_slot, 1, "selected B");
	TEST_NEQ(ctx.flags & VB2_CONTEXT_FW_SLOT_B, 0, "ctx says choose B");

	/* Slot A used up a try */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_TRY_COUNT, 3);
	TEST_SUCC(vb2_select_fw_slot(&ctx), "try slot A");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_RESULT),
		VB2_FW_RESULT_TRYING, "result trying");
	TEST_NEQ(sd->status & VB2_SD_STATUS_CHOSE_SLOT, 0, "chose slot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_TRIED), 0, "tried A");
	TEST_EQ(sd->fw_slot, 0, "selected A");
	TEST_EQ(ctx.flags & VB2_CONTEXT_FW_SLOT_B, 0, "didn't choose B");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_COUNT), 2, "tries decremented");

	/* Slot A failed, but nofail active */
	reset_common_data();
	ctx.flags |= VB2_CONTEXT_NOFAIL_BOOT;
	vb2_nv_set(&ctx, VB2_NV_TRY_COUNT, 3);
	TEST_SUCC(vb2_select_fw_slot(&ctx), "try slot A");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_RESULT),
		VB2_FW_RESULT_TRYING, "result trying");
	TEST_NEQ(sd->status & VB2_SD_STATUS_CHOSE_SLOT, 0, "chose slot");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_TRIED), 0, "tried A");
	TEST_EQ(sd->fw_slot, 0, "selected A");
	TEST_EQ(ctx.flags & VB2_CONTEXT_FW_SLOT_B, 0, "didn't choose B");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_TRY_COUNT), 3, "tries not decremented");

	/* Tried/result get copied to the previous fields */
	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_FW_TRIED, 0);
	vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_SUCCESS);
	vb2_select_fw_slot(&ctx);
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_PREV_TRIED), 0, "prev A");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_PREV_RESULT),	VB2_FW_RESULT_SUCCESS,
		"prev success");

	reset_common_data();
	vb2_nv_set(&ctx, VB2_NV_FW_TRIED, 1);
	vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_FAILURE);
	vb2_select_fw_slot(&ctx);
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_PREV_TRIED), 1, "prev B");
	TEST_EQ(vb2_nv_get(&ctx, VB2_NV_FW_PREV_RESULT),	VB2_FW_RESULT_FAILURE,
		"prev failure");
}

int main(int argc, char* argv[])
{
	init_context_tests();
	misc_tests();
	gbb_tests();
	fail_tests();
	recovery_tests();
	dev_switch_tests();
	tpm_clear_tests();
	select_slot_tests();

	return gTestSuccess ? 0 : 255;
}
