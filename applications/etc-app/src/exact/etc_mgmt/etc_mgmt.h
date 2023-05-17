#ifndef ETC_MGMT_H_
#define ETC_MGMT_H_

#define MGMT_GROUP_ID_ETC               65
/**
 * Command id for custom fs implementation
 */
#define ETC_MGMT_ID_RECORD_STATUS       0
#define ETC_MGMT_ID_RECORD_READ         1
#define ETC_MGMT_ID_RECORD_CLEAN        2
#define ETC_MGMT_ID_RECORD_RECLAIM      3

void etc_mgmt_register_group(void);

#endif
