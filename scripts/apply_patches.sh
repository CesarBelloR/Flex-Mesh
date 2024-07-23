# bin/bash

SCRIPT_PATH="$(dirname "$0")"
WDIR="$PWD"

cd ${SCRIPT_PATH}/../../zephyr && git am ${WDIR}/${SCRIPT_PATH}/../patch/zephyr/*.patch