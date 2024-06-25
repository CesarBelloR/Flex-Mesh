# bin/bash

SCRIPT_PATH="$(dirname "$0")"
WDIR="$PWD"

cd ${SCRIPT_PATH}/../../zephyr && git am ${WDIR}/${SCRIPT_PATH}/../patch/zephyr/*.patch
cd ${SCRIPT_PATH}/../../nrf && git am ${WDIR}/${SCRIPT_PATH}/../patch/nrf/*.patch