#ifndef APP_VERSION_H_
#define APP_VERSION_H_

/** Application major version. */
#define APP_VERSION_MAJOR 0
/** Application minor version. */
#define APP_VERSION_MINOR 0
/** Application patch version. */
#define APP_VERSION_PATCH 0

/** Application version. */
#define APP_VERSION \
	((APP_VERSION_MAJOR << 16) + \
	 (APP_VERSION_MINOR << 8) + \
	  APP_VERSION_PATCH)

/** Application version (string). */
#define APP_VERSION_STRING "0.0.0-twister"

/** Numerical Application version string (MAJOR.MINOR.PATCH) without suffix (e.g. -rc) */
#define APP_VERSION_NUM_STR "0.0.0"

#endif /* APP_VERSION_H_ */
