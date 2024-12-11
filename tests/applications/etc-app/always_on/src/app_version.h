/**
 * @file app_version.h
 *
 * Application version information.
 *
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_VERSION_H_
#define APP_VERSION_H_

/** Application major version. */
#define APP_VERSION_MAJOR 1
/** Application minor version. */
#define APP_VERSION_MINOR 2
/** Application patch version. */
#define APP_VERSION_PATCH 2

/** Application version. */
#define APP_VERSION \
	((APP_VERSION_MAJOR << 16) + \
	 (APP_VERSION_MINOR << 8) + \
	  APP_VERSION_PATCH)

/** Application version (string). */
#define APP_VERSION_STRING "1.2.2-rc1"

/** Numerical Application version string (MAJOR.MINOR.PATCH) without suffix (e.g. -rc) */
#define APP_VERSION_NUM_STR "1.2.2"

#endif /* APP_VERSION_H_ */
