/***************************************************************************/
/*!
\file       etc_app.c
\brief      ETC application (relay/logger)

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <device.h>
#include <errno.h>
#include <sys/util.h>
#include <zephyr.h>
#include <kernel.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(ETC_APP, CONFIG_ETC_APP_LOG_LEVEL);

#include "etc_setting.h"
#include "etc_lora.h"
#include "etc_record.h"

static void log_work_handler(struct k_work *work);
static void transmit_work_handler(struct k_work *work);
static struct k_work_delayable log_work;
static struct k_work_delayable transmit_work;

static void etc_lora_rx_handler(void* data, int length) {
    int rc = etc_record_put(data, length);
    if (rc != 0) {
        LOG_ERR("Failed to put record to queue"); 
    }
}

int etc_app_init(void) {
    int rc = etc_lora_init();
    if (rc != 0) {
        LOG_ERR("Failed to LORA service");
        return rc;
    }

    rc = etc_record_init();
    if (rc != 0) {
        LOG_ERR("Failed to initialize record manager");
        return rc;
    }

    k_work_init_delayable(&log_work, log_work_handler);
    k_work_init_delayable(&transmit_work, transmit_work_handler);
    if (p_etc_config->device_mode == ETC_DEVICE_MODE_RELAY) {
        etc_lora_receive(etc_lora_rx_handler);
    } else if (p_etc_config->device_mode == ETC_DEVICE_MODE_LOGGER) {
        
    } else {
        LOG_ERR("Unknown device mode %d", p_etc_config->device_mode);
    }

    return 0;
}

int etc_app_run(void) {
    return 0;
}

static void log_work_handler(struct k_work *work) {

}

static void transmit_work_handler(struct k_work *work) {
    
}