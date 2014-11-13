/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for firmware image library.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "file_keys.h"
#include "host_common.h"
#include "vb2_convert_structs.h"
#include "vboot_common.h"
#include "test_common.h"

#include "2common.h"
#include "2rsa.h"

static const uint8_t test_data[] = "This is some test data to sign.";
static const uint32_t test_size = sizeof(test_data);

static void test_unpack_key(const struct vb2_packed_key *key1)
{
	struct vb2_public_key pubk;

	/*
	 * Key data follows the header for a newly allocated key, so we can
	 * calculate the buffer size by looking at how far the key data goes.
	 */
	uint32_t size = key1->key_offset + key1->key_size;
	uint8_t *buf = malloc(size);
	struct vb2_packed_key *key = (struct vb2_packed_key *)buf;

	memcpy(key, key1, size);
	TEST_SUCC(vb2_unpack_key(&pubk, buf, size), "vb2_unpack_key() ok");

	TEST_EQ(pubk.sig_alg, vb2_crypto_to_signature(key->algorithm),
		"vb2_unpack_key() sig_alg");
	TEST_EQ(pubk.hash_alg, vb2_crypto_to_hash(key->algorithm),
		"vb2_unpack_key() hash_alg");


	memcpy(key, key1, size);
	key->algorithm = VB2_ALG_COUNT;
	TEST_EQ(vb2_unpack_key(&pubk, buf, size),
		VB2_ERROR_UNPACK_KEY_SIG_ALGORITHM,
		"vb2_unpack_key() invalid algorithm");

	memcpy(key, key1, size);
	key->key_size--;
	TEST_EQ(vb2_unpack_key(&pubk, buf, size),
		VB2_ERROR_UNPACK_KEY_SIZE,
		"vb2_unpack_key() invalid size");

	memcpy(key, key1, size);
	key->key_offset++;
	TEST_EQ(vb2_unpack_key(&pubk, buf, size + 1),
		VB2_ERROR_UNPACK_KEY_ALIGN,
		"vb2_unpack_key() unaligned data");

	memcpy(key, key1, size);
	*(uint32_t *)(buf + key->key_offset) /= 2;
	TEST_EQ(vb2_unpack_key(&pubk, buf, size),
		VB2_ERROR_UNPACK_KEY_ARRAY_SIZE,
		"vb2_unpack_key() invalid key array size");

	memcpy(key, key1, size);
	TEST_EQ(vb2_unpack_key(&pubk, buf, size - 1),
		VB2_ERROR_INSIDE_DATA_OUTSIDE,
		"vb2_unpack_key() buffer too small");

	free(key);
}

static void test_unpack_key2(const struct vb2_packed_key *key1)
{
	struct vb2_public_key pubk;
	struct vb2_packed_key2 *key2;
	uint32_t size;

	/* Should be able to handle a vboot1-style key binary as well */
	TEST_SUCC(vb2_unpack_key2(&pubk, (uint8_t *)key1,
				  key1->key_offset + key1->key_size),
		  "vb2_unpack_key2() passthru");

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	TEST_SUCC(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		  "vb2_unpack_key2() ok");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->key_offset += 4;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_COMMON_MEMBER_SIZE,
		"vb2_unpack_key2() buffer too small");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->c.fixed_size += size;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_COMMON_FIXED_SIZE,
		"vb2_unpack_key2() buffer too small for desc");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->c.desc_size = 0;
	TEST_SUCC(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		  "vb2_unpack_key2() no desc");
	TEST_EQ(strcmp(pubk.desc, ""), 0, "  empty desc string");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->c.magic++;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_INSIDE_DATA_OUTSIDE,
		"vb2_unpack_key2() bad magic");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->c.struct_version_major++;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_UNPACK_KEY_STRUCT_VERSION,
		"vb2_unpack_key2() bad major version");
	free(key2);

	/*
	 * Minor version changes are ok.  Note that this test assumes that the
	 * source key struct version is the highest actually known to the
	 * reader.  If the reader does know about minor version + 1 and that
	 * adds fields, this test will likely fail.  But at that point, we
	 * should have already added a test for minor version compatibility to
	 * handle both old and new struct versions, so someone will have
	 * noticed this comment.
	 */
	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->c.struct_version_minor++;
	TEST_SUCC(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		  "vb2_unpack_key2() minor version change ok");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->sig_alg = VB2_SIG_INVALID;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_UNPACK_KEY_SIG_ALGORITHM,
		"vb2_unpack_key2() bad sig algorithm");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->hash_alg = VB2_HASH_INVALID;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_UNPACK_KEY_HASH_ALGORITHM,
		"vb2_unpack_key2() bad hash algorithm");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->key_size -= 4;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_UNPACK_KEY_SIZE,
		"vb2_unpack_key2() invalid size");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	key2->key_offset--;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_COMMON_MEMBER_UNALIGNED,
		"vb2_unpack_key2() unaligned data");
	free(key2);

	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	*(uint32_t *)((uint8_t *)key2 + key2->key_offset) /= 2;
	TEST_EQ(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		VB2_ERROR_UNPACK_KEY_ARRAY_SIZE,
		"vb2_unpack_key2() invalid key array size");
	free(key2);
}

static void test_verify_data(struct vb2_packed_key *key1,
			     const struct vb2_signature *sig)
{
	uint8_t workbuf[VB2_VERIFY_DATA_WORKBUF_BYTES];
	struct vb2_workbuf wb;

	uint32_t pubkey_size = key1->key_offset + key1->key_size;
	struct vb2_public_key pubk, pubk_orig;
	uint32_t sig_total_size = sig->sig_offset + sig->sig_size;
	struct vb2_signature *sig2;

	vb2_workbuf_init(&wb, workbuf, sizeof(workbuf));

	/* Allocate signature copy for tests */
	sig2 = (struct vb2_signature *)malloc(sig_total_size);

	TEST_EQ(vb2_unpack_key(&pubk, (uint8_t *)key1, pubkey_size),
		0, "vb2_verify_data() unpack key");
	pubk_orig = pubk;

	memcpy(sig2, sig, sig_total_size);
	pubk.sig_alg = VB2_SIG_INVALID;
	TEST_NEQ(vb2_verify_data(test_data, test_size, sig2, &pubk, &wb),
		 0, "vb2_verify_data() bad sig alg");
	pubk.sig_alg = pubk_orig.sig_alg;

	memcpy(sig2, sig, sig_total_size);
	pubk.hash_alg = VB2_HASH_INVALID;
	TEST_NEQ(vb2_verify_data(test_data, test_size, sig2, &pubk, &wb),
		 0, "vb2_verify_data() bad hash alg");
	pubk.hash_alg = pubk_orig.hash_alg;

	vb2_workbuf_init(&wb, workbuf, 4);
	memcpy(sig2, sig, sig_total_size);
	TEST_NEQ(vb2_verify_data(test_data, test_size, sig2, &pubk, &wb),
		 0, "vb2_verify_data() workbuf too small");
	vb2_workbuf_init(&wb, workbuf, sizeof(workbuf));

	memcpy(sig2, sig, sig_total_size);
	TEST_EQ(vb2_verify_data(test_data, test_size, sig2, &pubk, &wb),
		0, "vb2_verify_data() ok");

	memcpy(sig2, sig, sig_total_size);
	sig2->sig_size -= 16;
	TEST_NEQ(vb2_verify_data(test_data, test_size, sig2, &pubk, &wb),
		 0, "vb2_verify_data() wrong sig size");

	memcpy(sig2, sig, sig_total_size);
	TEST_NEQ(vb2_verify_data(test_data, test_size - 1, sig2, &pubk, &wb),
		 0, "vb2_verify_data() input buffer too small");

	memcpy(sig2, sig, sig_total_size);
	vb2_signature_data(sig2)[0] ^= 0x5A;
	TEST_NEQ(vb2_verify_data(test_data, test_size, sig2, &pubk, &wb),
		 0, "vb2_verify_data() wrong sig");

	free(sig2);
}

static void test_verify_signature(const struct vb2_packed_key *key1,
				  const struct vb2_signature *sig1)
{
	struct vb2_public_key pubk;
	struct vb2_packed_key2 *key2;
	struct vb2_signature2 *sig2;
	uint8_t *buf2good, *buf2;
	uint32_t size;

	/* Unpack and convert the public key */
	TEST_SUCC(vb2_unpack_key2(&pubk, (uint8_t *)key1,
				  key1->key_offset + key1->key_size),
		  "verify_sig vb2_unpack_key2() passthru");
	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	TEST_PTR_NEQ(key2, 0, "verify_sig convert pub key");

	buf2good = (uint8_t *)
		vb2_convert_signature2(sig1, "test desc", key2, &size);
	buf2 = malloc(size);
	sig2 = (struct vb2_signature2 *)buf2;

	memcpy(buf2, buf2good, size);
	TEST_PTR_NEQ(sig1, 0, "verify_sig convert signature");
	TEST_SUCC(vb2_verify_signature2(sig2, size), "verify_sig ok");
	sig2->c.magic = VB2_MAGIC_PACKED_KEY2;
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_SIG_MAGIC,
		"verify_sig magic");

	memcpy(buf2, buf2good, size);
	sig2->c.total_size += 4;
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_COMMON_TOTAL_SIZE,
		"verify_sig common header");

	memcpy(buf2, buf2good, size);
	sig2->c.struct_version_minor++;
	TEST_SUCC(vb2_verify_signature2(sig2, size), "verify_sig minor ver");
	sig2->c.struct_version_major++;
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_SIG_VERSION,
		"verify_sig major ver");

	memcpy(buf2, buf2good, size);
	sig2->c.fixed_size -= 4;
	sig2->c.desc_size += 4;
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_SIG_HEADER_SIZE,
		"verify_sig header size");

	memcpy(buf2, buf2good, size);
	sig2->sig_size += 4;
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_COMMON_MEMBER_SIZE,
		"verify_sig sig size");

	memcpy(buf2, buf2good, size);
	sig2->sig_alg = VB2_SIG_INVALID;
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_SIG_ALGORITHM,
		"verify_sig sig alg");

	memcpy(buf2, buf2good, size);
	sig2->sig_alg = (sig2->sig_alg == VB2_SIG_NONE ?
			 VB2_SIG_RSA1024 : VB2_SIG_NONE);
	TEST_EQ(vb2_verify_signature2(sig2, size), VB2_ERROR_SIG_SIZE,
		"verify_sig sig size");

	free(buf2);
	free(buf2good);
	free(key2);
}

static void test_verify_data2(struct vb2_packed_key *key1,
			     const struct vb2_signature *sig1)
{
	uint8_t workbuf[VB2_VERIFY_DATA_WORKBUF_BYTES];
	struct vb2_workbuf wb;

	struct vb2_public_key pubk, pubk_orig;
	struct vb2_packed_key2 *key2;
	struct vb2_signature2 *sig2;
	uint8_t *buf2good, *buf2;
	uint32_t size;

	vb2_workbuf_init(&wb, workbuf, sizeof(workbuf));

	/* Unpack and convert the public key */
	key2 = vb2_convert_packed_key2(key1, "Test key", &size);
	TEST_PTR_NEQ(key2, 0, "verify_data convert pub key");
	TEST_SUCC(vb2_unpack_key2(&pubk, (uint8_t *)key2, size),
		  "verify_data2 unpack key");
	pubk_orig = pubk;

	/* Convert signature and allocate copy for tests */
	buf2good = (uint8_t *)
		vb2_convert_signature2(sig1, "test desc", key2, &size);
	buf2 = malloc(size);
	sig2 = (struct vb2_signature2 *)buf2;

	memcpy(buf2, buf2good, size);
	pubk.sig_alg = VB2_SIG_INVALID;
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		VB2_ERROR_VDATA_ALGORITHM, "vb2_verify_data2() bad sig alg");
	pubk.sig_alg = pubk_orig.sig_alg;

	memcpy(buf2, buf2good, size);
	pubk.hash_alg = VB2_HASH_INVALID;
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		VB2_ERROR_VDATA_DIGEST_SIZE,
		"vb2_verify_data2() bad hash alg");
	pubk.hash_alg = pubk_orig.hash_alg;

	vb2_workbuf_init(&wb, workbuf, 4);
	memcpy(buf2, buf2good, size);
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		VB2_ERROR_VDATA_WORKBUF_DIGEST,
		"vb2_verify_data2() workbuf too small");
	vb2_workbuf_init(&wb, workbuf, sizeof(workbuf));

	memcpy(buf2, buf2good, size);
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		0, "vb2_verify_data2() ok");

	memcpy(buf2, buf2good, size);
	sig2->sig_size -= 16;
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		VB2_ERROR_VDATA_SIG_SIZE, "vb2_verify_data2() wrong sig size");

	memcpy(buf2, buf2good, size);
	TEST_EQ(vb2_verify_data2(test_data, test_size - 1, sig2, &pubk, &wb),
		VB2_ERROR_VDATA_SIZE, "vb2_verify_data2() wrong data size");

	memcpy(buf2, buf2good, size);
	sig2->hash_alg = (sig2->hash_alg == VB2_HASH_SHA1 ?
			  VB2_HASH_SHA256 : VB2_HASH_SHA1);
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		VB2_ERROR_VDATA_ALGORITHM_MISMATCH,
		"vb2_verify_data2() alg mismatch");


	memcpy(buf2, buf2good, size);
	buf2[sig2->sig_offset] ^= 0x5A;
	TEST_EQ(vb2_verify_data2(test_data, test_size, sig2, &pubk, &wb),
		VB2_ERROR_RSA_PADDING, "vb2_verify_data2() wrong sig");

	free(buf2);
	free(buf2good);
}

int test_algorithm(int key_algorithm, const char *keys_dir)
{
	char filename[1024];
	int rsa_len = siglen_map[key_algorithm] * 8;

	VbPrivateKey *private_key = NULL;

	struct vb2_signature *sig = NULL;
	struct vb2_packed_key *key1;

	printf("***Testing algorithm: %s\n", algo_strings[key_algorithm]);

	sprintf(filename, "%s/key_rsa%d.pem", keys_dir, rsa_len);
	private_key = PrivateKeyReadPem(filename, key_algorithm);
	if (!private_key) {
		fprintf(stderr, "Error reading private_key: %s\n", filename);
		return 1;
	}

	sprintf(filename, "%s/key_rsa%d.keyb", keys_dir, rsa_len);
	key1 = (struct vb2_packed_key *)
		PublicKeyReadKeyb(filename, key_algorithm, 1);
	if (!key1) {
		fprintf(stderr, "Error reading public_key: %s\n", filename);
		return 1;
	}

	/* Calculate good signature */
	sig = (struct vb2_signature *)
		CalculateSignature(test_data, sizeof(test_data), private_key);
	TEST_PTR_NEQ(sig, 0, "Calculate signature");
	if (!sig)
		return 1;

	test_unpack_key(key1);
	test_unpack_key2(key1);
	test_verify_data(key1, sig);
	test_verify_data2(key1, sig);
	test_verify_signature(key1, sig);

	if (key1)
		free(key1);
	if (private_key)
		free(private_key);
	if (sig)
		free(sig);

	return 0;
}

/* Test only the algorithms we use */
const int key_algs[] = {
	VB2_ALG_RSA2048_SHA256,
	VB2_ALG_RSA4096_SHA256,
	VB2_ALG_RSA8192_SHA512,
};

int main(int argc, char *argv[]) {

	if (argc == 2) {
		int i;

		for (i = 0; i < ARRAY_SIZE(key_algs); i++) {
			if (test_algorithm(key_algs[i], argv[1]))
				return 1;
		}

	} else if (argc == 3 && !strcasecmp(argv[2], "--all")) {
		/* Test all the algorithms */
		int alg;

		for (alg = 0; alg < kNumAlgorithms; alg++) {
			if (test_algorithm(alg, argv[1]))
				return 1;
		}

	} else {
		fprintf(stderr, "Usage: %s <keys_dir> [--all]", argv[0]);
		return -1;
	}

	return gTestSuccess ? 0 : 255;
}