/*
 * Copyright (c) 2026 EXACT Technology
 */

#ifndef THRESHOLD_OBJECT_H__
#define THRESHOLD_OBJECT_H__

#define ETC_THRESHOLD_OBJECT_ID 48944

/* One instance per threshold slot, created by the device at boot. */
#define ETC_THRESHOLD_OBJ_MAX_INSTANCE_COUNT 4

/* Threshold object resource IDs */
#define ETC_THRESHOLD_OBJ_R_ENABLED	    1U
#define ETC_THRESHOLD_OBJ_R_VALUE_TYPE	    2U
#define ETC_THRESHOLD_OBJ_R_ALERT_TYPE	    3U
#define ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE 4U
#define ETC_THRESHOLD_OBJ_R_ALERT	    5U
#define ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED  6U
#define ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT   7U

/* Flex 2.0 value type enumeration bounds */
#define ETC_THRESHOLD_OBJ_R_VALUE_TYPE_MIN_VAL 1U
#define ETC_THRESHOLD_OBJ_R_VALUE_TYPE_MAX_VAL 12U
/* 0 = exceeds, 1 = drops below */
#define ETC_THRESHOLD_OBJ_R_ALERT_TYPE_MIN_VAL 0U
#define ETC_THRESHOLD_OBJ_R_ALERT_TYPE_MAX_VAL 1U

#endif /* THRESHOLD_OBJECT_H__ */
