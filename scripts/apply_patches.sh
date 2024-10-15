# bin/bash

SCRIPT_PATH="$(dirname "$0")"
WDIR="$PWD"

cd ${SCRIPT_PATH}/../../zephyr && git am ${WDIR}/${SCRIPT_PATH}/../patch/zephyr/*.patch
cd ${WDIR}/${SCRIPT_PATH}/../../modules/lib/memfault-firmware-sdk && git am ${WDIR}/${SCRIPT_PATH}/../patch/modules/lib/memfault-firmware-sdk/*.patch