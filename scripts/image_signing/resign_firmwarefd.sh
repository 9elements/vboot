#!/bin/sh

# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to resign a firmware image using a different set of keys
# for use on signing servers.
#
# arguments: src_fd, dst_fd, firmware_datakey, and firmware_keyblock
#
# src_fd: Input firmware image (in .fd format)
# dst_fd: output firmware image name
# firmware_datakey: Key used to sign firmware data (in .vbprivk format)
# firmware_keyblock: Key block for firmware data key (in .keyblock format)
#
# Both the dump_fmap tool and vbutil_firmware should be in the system path.
#
# This script parses the output of dump_fmap tool
#
# to determine the regions in the image containing "Firmware [A|B] Data" and
# "Firmware [A|B] Key", which contain firmware data and firmware vblocks
# respectively. It will then generate new vblocks using the set of keys
# passed as arguments and output a new firmware image, with this new firmware
# vblocks the old ones.
#
# Here is an example output of "dump_fmap -p" (section name, offset, size):
# Boot Stub 3932160 262144
# GBB Area 3670016 262144
# Recovery Firmware 2490368 1048576
# RO VPD 2359296 131072
# Firmware A Key 196608 65536
# Firmware A Data 262144 851968
# Firmware B Key 1114112 65536
# Firmware B Data 1179648 851968
# RW VPD 152064 4096
# Log Volume 0 131072
#
# This shows that Firmware A Data is at offset 262144(0x40000) in the .fd image
# and is of size 851968(0xd0000) bytes. This can be extracted to generate new
# vblocks which can then replace old vblock for Firmware A ("Firmware A Key"
# region at offset 196608(0x30000) and size 65536(0x10000) ).

# Load common constants and variables.
. "$(dirname "$0")/common_minimal.sh"

# Abort on error
set -e

# Check arguments
if [ $# -lt 7 ] || [ $# -gt 11 ]; then
  echo "Usage: $PROG src_fd dst_fd firmware_datakey firmware_keyblock" \
    "dev_firmware_datakey dev_firmware_keyblock kernel_subkey [version]" \
    "[flag] [loem_output_dir] [loemid]" \
  exit 1
fi

# Make sure the tools we need are available.
for prog in cut dump_fmap md5sum vbutil_firmware; do
  type "${prog}" &>/dev/null || \
    { echo "${prog} tool not found."; exit 1; }
done

SRC_FD=$1
DST_FD=$2
FIRMWARE_DATAKEY=$3
FIRMWARE_KEYBLOCK=$4
DEV_FIRMWARE_DATAKEY=$5
DEV_FIRMWARE_KEYBLOCK=$6
KERNEL_SUBKEY=$7
VERSION=$8
# This is the --flags in vbutil_firmware. It currently has only two values:
# 0 for RW-NORMAL firmware, and 1 for RO-NORMAL firmware (search "two_stop
# firmware" for more information).
PREAMBLE_FLAG=$9
LOEM_OUTPUT_DIR=${10}
LOEMID=${11}

# Disables using developer keyblocks
disable_dev_keyblock() {
  DEV_FIRMWARE_KEYBLOCK=$FIRMWARE_KEYBLOCK
  DEV_FIRMWARE_DATAKEY=$FIRMWARE_DATAKEY
}

# Compares if the contents in given files are the same.
is_the_same_binary_file() {
  local hash1="$(md5sum -b "$1" | cut -d' ' -f1)"
  local hash2="$(md5sum -b "$2" | cut -d' ' -f1)"
  [ "$hash1" = "$hash2" ]
}

# Extract firmware body section from SRC_FD and truncate it to its body size
extract_firmware_image() {
  local label="$1"
  local root_key="$2"
  local vblock_offset="$3"
  local vblock_size="$4"
  local vblock_image="$5"
  local fw_offset="$6"
  local fw_size="$7"
  local fw_image="$8"
  local fw_body_size=""

  dd if="${SRC_FD}" of="${vblock_image}" skip="${vblock_offset}" bs=1 \
    count="${vblock_size}" 2>/dev/null
  dd if="${SRC_FD}" of="${fw_image}" skip="${fw_offset}" bs=1 \
    count="${fw_size}" 2>/dev/null
  fw_body_size="$(vbutil_firmware \
    --verify "${vblock_image}" \
    --signpubkey "${root_key}" \
    --fv "${fw_image}" |
    grep "Firmware body size:" |
    sed 's/.*: *//')" || fw_body_size="${fw_size}"
  if [ "${fw_body_size}" -gt "${fw_size}" ]; then
    echo -n "Firmware ${label} body size exceeds its section: "
    echo    "${fw_body_size} > ${fw_size}"
    return 1
  elif [ "${fw_body_size}" -lt "${fw_size}" ]; then
    dd if="${SRC_FD}" of="${fw_image}" skip="${fw_offset}" bs=1 \
      count="${fw_body_size}" 2>/dev/null
  fi
}

if [ -z "$VERSION" ]; then
  VERSION=1
fi
echo "Using firmware version: $VERSION"

if [ ! -e $DEV_FIRMWARE_KEYBLOCK ] || [ ! -e $DEV_FIRMWARE_DATAKEY ] ; then
  echo "No dev firmware keyblock/datakey found. Reusing normal keys."
  disable_dev_keyblock
fi

# Parse offsets and size of firmware data and vblocks
for i in "A" "B"
do
  line=$(dump_fmap -p $1 | grep "^Firmware $i Key") ||
  line=$(dump_fmap -p $1 | grep "^VBLOCK_$i") ||
   { echo "Couldn't parse vblock section $i from dump_fmap output";
     exit 1; }

  offset="$(echo $line | sed -r -e 's/.* ([0-9]+) [0-9]+$/\1/')"
  eval fw${i}_vblock_offset=$((offset))
  size="$(echo $line | sed -r -e 's/.* [0-9]+ ([0-9]+)$/\1/')"
  eval fw${i}_vblock_size=$((size))

  line=$(dump_fmap -p $1 | grep "^Firmware $i Data") ||
  line=$(dump_fmap -p $1 | grep "^FW_MAIN_$i") ||
  { echo "Couldn't parse Firmware $i section from dump_fmap output";
    exit 1; }

  offset="$(echo $line | sed -r -e 's/.* ([0-9]+) [0-9]+$/\1/')"
  eval fw${i}_offset=$((offset))
  size="$(echo $line | sed -r -e 's/.* [0-9]+ ([0-9]+)$/\1/')"
  eval fw${i}_size=$((size))
done

temp_fwimage_a=$(make_temp_file)
temp_fwimage_b=$(make_temp_file)
temp_out_vb=$(make_temp_file)
temp_root_key=$(make_temp_file)

echo "Reading Root Key from GBB"
gbb_utility -g --rootkey="$temp_root_key" "${SRC_FD}"

echo "Extracting Firmware A and B"
extract_firmware_image "A" "${temp_root_key}" \
  "${fwA_vblock_offset}" "${fwA_vblock_size}" "${temp_out_vb}" \
  "${fwA_offset}" "${fwA_size}" "${temp_fwimage_a}"
extract_firmware_image "B" "${temp_root_key}" \
  "${fwB_vblock_offset}" "${fwB_vblock_size}" "${temp_out_vb}" \
  "${fwB_offset}" "${fwB_size}" "${temp_fwimage_b}"

echo "Determining preamble flag from existing firmware"
if [ -n "$PREAMBLE_FLAG" ]; then
  PREAMBLE_FLAG="--flags $PREAMBLE_FLAG"
else
  dd if="${SRC_FD}" of="${temp_out_vb}" skip="${fwA_vblock_offset}" bs=1 \
    count="${fwA_vblock_size}" 2>/dev/null
  flag="$(vbutil_firmware \
    --verify "${temp_out_vb}" \
    --signpubkey "${temp_root_key}" \
    --fv "${temp_fwimage_a}" |
    grep "Preamble flags:" |
    sed 's/.*: *//')" || flag=""
  [ -z "$flag" ] || PREAMBLE_FLAG="--flags $flag"
fi
echo "Using firmware preamble flag: $PREAMBLE_FLAG"

# Sanity check firmware type: "developer key block" should be only used if the
# content in firmware A/B are different; otherwise always use normal key blocks.
if is_the_same_binary_file "${temp_fwimage_a}" "${temp_fwimage_b}"; then
  echo "Found firmware with same A/B content - ignoring DEV keyblock."
  disable_dev_keyblock
fi

echo "Re-calculating Firmware A vblock"
vbutil_firmware \
  --vblock "${temp_out_vb}" \
  --keyblock "${DEV_FIRMWARE_KEYBLOCK}" \
  --signprivate "${DEV_FIRMWARE_DATAKEY}" \
  --version "${VERSION}" \
  $PREAMBLE_FLAG \
  --fv "${temp_fwimage_a}" \
  --kernelkey "${KERNEL_SUBKEY}"

if [ -z "${LOEMID}" ]; then
  # Create a copy of the input image and put in the new vblock for firmware A
  cp "${SRC_FD}" "${DST_FD}"
  dd if="${temp_out_vb}" of="${DST_FD}" seek="${fwA_vblock_offset}" bs=1 \
    count="${fwA_vblock_size}" conv=notrunc 2>/dev/null
else
  cp "${temp_out_vb}" "${LOEM_OUTPUT_DIR}/vblock_A.${LOEMID}"
fi

echo "Re-calculating Firmware B vblock"
vbutil_firmware \
  --vblock "${temp_out_vb}" \
  --keyblock "${FIRMWARE_KEYBLOCK}" \
  --signprivate "${FIRMWARE_DATAKEY}" \
  --version "${VERSION}" \
  $PREAMBLE_FLAG \
  --fv "${temp_fwimage_b}" \
  --kernelkey "${KERNEL_SUBKEY}"

if [[ -z ${LOEMID} ]]; then
  # Destination image has already been created.
  dd if="${temp_out_vb}" of="${DST_FD}" seek="${fwB_vblock_offset}" bs=1 \
    count="${fwB_vblock_size}" conv=notrunc 2>/dev/null
else
  cp "${temp_out_vb}" "${LOEM_OUTPUT_DIR}/vblock_A.${LOEMID}"
fi

echo "New signed image was output to ${DST_FD}"
