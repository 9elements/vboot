#!/bin/bash
# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Generate .vbpubk and .vbprivk pairs for use by developer builds. These should
# be exactly like the real keys except that the private keys aren't secret.

# Load common constants and functions.
. "$(dirname "$0")/common.sh"

usage() {
  cat <<EOF
Usage: $0 [--devkeyblock]

Options:
  --devkeyblock   Also generate developer firmware keyblock and data key
EOF

  if [[ $# -ne 0 ]]; then
    echo "ERROR: unknown option $*" >&2
    exit 1
  else
    exit 0
  fi
}

main() {
  set -e

  # Flag to indicate whether we should be generating a developer keyblock flag.
  local dev_keyblock="false"
  while [[ $# -gt 0 ]]; do
    case $1 in
    --devkeyblock)
      echo "Will also generate developer firmware keyblock and data key."
      dev_keyblock="true"
      ;;
    -h|--help)
      usage
      ;;
    *)
      usage "$1"
      ;;
    esac
    shift
  done

  if [[ ! -e "${VERSION_FILE}" ]]; then
    echo "No version file found. Creating default ${VERSION_FILE}."
    printf '%s_version=1\n' {firmware,kernel}{_key,} > "${VERSION_FILE}"
  fi

  local eckey_version fkey_version ksubkey_version kdatakey_version

  # Get the key versions for normal keypairs
  eckey_version=$(get_version "ec_key_version")
  fkey_version=$(get_version "firmware_key_version")
  # Firmware version is the kernel subkey version.
  ksubkey_version=$(get_version "firmware_version")
  # Kernel data key version is the kernel key version.
  kdatakey_version=$(get_version "kernel_key_version")

  # Create the normal keypairs
  make_pair ec_root_key              ${EC_ROOT_KEY_ALGOID}
  make_pair ec_data_key              ${EC_DATAKEY_ALGOID} ${eckey_version}
  make_pair root_key                 ${ROOT_KEY_ALGOID}
  make_pair firmware_data_key        ${FIRMWARE_DATAKEY_ALGOID} ${fkey_version}
  if [[ "${dev_keyblock}" == "true" ]]; then
    make_pair dev_firmware_data_key    ${DEV_FIRMWARE_DATAKEY_ALGOID} ${fkey_version}
  fi
  make_pair kernel_subkey            ${KERNEL_SUBKEY_ALGOID} ${ksubkey_version}
  make_pair kernel_data_key          ${KERNEL_DATAKEY_ALGOID} ${kdatakey_version}

  # Create the recovery and factory installer keypairs
  make_pair recovery_key             ${RECOVERY_KEY_ALGOID}
  make_pair recovery_kernel_data_key ${RECOVERY_KERNEL_ALGOID}
  make_pair installer_kernel_data_key ${INSTALLER_KERNEL_ALGOID}

  # Create the firmware keyblock for use only in Normal mode. This is redundant,
  # since it's never even checked during Recovery mode.
  make_keyblock firmware ${FIRMWARE_KEYBLOCK_MODE} firmware_data_key root_key
  # Ditto EC keyblock
  make_keyblock ec ${EC_KEYBLOCK_MODE} ec_data_key ec_root_key

  if [[ "${dev_keyblock}" == "true" ]]; then
    # Create the dev firmware keyblock for use only in Developer mode.
    make_keyblock dev_firmware ${DEV_FIRMWARE_KEYBLOCK_MODE} dev_firmware_data_key root_key
  fi

  # Create the recovery kernel keyblock for use only in Recovery mode.
  make_keyblock recovery_kernel ${RECOVERY_KERNEL_KEYBLOCK_MODE} recovery_kernel_data_key recovery_key

  # Create the normal kernel keyblock for use only in Normal mode.
  make_keyblock kernel ${KERNEL_KEYBLOCK_MODE} kernel_data_key kernel_subkey

  # Create the installer keyblock for use in Developer + Recovery mode
  # For use in Factory Install and Developer Mode install shims.
  make_keyblock installer_kernel ${INSTALLER_KERNEL_KEYBLOCK_MODE} installer_kernel_data_key recovery_key

  # CAUTION: The public parts of most of these blobs must be compiled into the
  # firmware, which is built separately (and some of which can't be changed after
  # manufacturing). If you update these keys, you must coordinate the changes
  # with the BIOS people or you'll be unable to boot the resulting images.
}
main "$@"
