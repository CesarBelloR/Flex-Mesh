#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(board, LOG_LEVEL_INF);

#include <nrfx_uarte.h>
#include <nrfx_twim.h>
#include <hal/nrf_gpio.h>

static void uart_disconnect_pins(NRF_UARTE_Type *p_reg)
{
	/* Reset pins to default states */
	uint32_t txd;
	uint32_t rxd;
	uint32_t rts;
	uint32_t cts;

	txd = nrf_uarte_tx_pin_get(p_reg);
	rxd = nrf_uarte_rx_pin_get(p_reg);
	rts = nrf_uarte_rts_pin_get(p_reg);
	cts = nrf_uarte_cts_pin_get(p_reg);
	nrf_uarte_txrx_pins_disconnect(p_reg);
	nrf_uarte_hwfc_pins_disconnect(p_reg);

	if (txd != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(txd);
	}
	if (rxd != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(rxd);
	}
	if (cts != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(cts);
	}
	if (rts != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(rts);
	}
}

static int errata_workaround(void)
{
	/* Work around for nRF52840 errata 
	 * [246] System: Intermittent extra current consumption when going to sleep
	 * https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev2/page/ERR/nRF52840/Rev2/latest/anomaly_840_246.html#anomaly_840_246 
	 */
	*(volatile uint32_t *)0x4007AC84ul = 0x00000002ul;

	return 0;
}

SYS_INIT(errata_workaround, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static void uninit_i2c(NRF_TWIM_Type *p_twim) 
{
	uint32_t scl;
	uint32_t sda;

	NRFX_IRQ_DISABLE(nrfx_get_irq_number(p_twim));
	nrf_twim_int_disable(p_twim, NRF_TWIM_ALL_INTS_MASK);
	nrf_twim_shorts_disable(p_twim, NRF_TWIM_ALL_SHORTS_MASK);
	nrf_twim_disable(p_twim);

	scl = nrf_twim_scl_pin_get(p_twim);
	sda = nrf_twim_sda_pin_get(p_twim);
	if (scl != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(scl);
	}
	if (sda != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(sda);
	}
}

static int peripheral_reset(void)
{
	/* Disable UART0 when application starts, if disabled in device tree.
	 * This fixes an issue with an older bootloader version where UART0
	 * is initialized and enabled.
	 */
	NRF_UARTE_Type *p_reg = NRF_UARTE0;
   	nrf_uarte_shorts_disable(p_reg, NRF_UARTE_SHORT_ENDRX_STARTRX);
	nrf_uarte_event_clear(p_reg, NRF_UARTE_EVENT_TXSTOPPED);
	nrf_uarte_task_trigger(p_reg, NRF_UARTE_TASK_STOPTX);
	nrf_uarte_disable(p_reg);
	uart_disconnect_pins(p_reg);

	/* Disable potentially previously initialized I2C interfaces */
	uninit_i2c(NRF_TWIM0);
	uninit_i2c(NRF_TWIM1);

	/* Reset PPI channel 18 */
	*(volatile uint32_t *)0x4001F5A0ul = 0x00000000ul;
	*(volatile uint32_t *)0x4001F5A4ul = 0x00000000ul;

	return 0;
}

/* Everything in peripheral_reset gets/sets values directly from MCU
 * registers. We can execute it as early as possible.
 */
SYS_INIT(peripheral_reset, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_OBJECTS);