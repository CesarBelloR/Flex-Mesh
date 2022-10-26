#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <app_version.h>
#include <drivers/hwinfo.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/socket.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

#define NET_CONFIG_PEER_IPV4_ADDR "116.109.123.155"
#define NET_PORT 8080

void main(void)
{
	int sock;
	int ret = 0;
	struct sockaddr_in addr4;
	struct sockaddr *addr = (struct sockaddr *)&addr4;
	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(NET_PORT);
	inet_pton(AF_INET, NET_CONFIG_PEER_IPV4_ADDR, &addr4.sin_addr);

	sock = socket(addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", -errno);
		return;
	}

	LOG_INF("Socket is ready %d", sock);
	/* Call connect so we can use send and recv. */
	ret = connect(sock, addr, sizeof(addr4));
	if (ret < 0) {
		LOG_ERR("Cannot connect to UDP remote %d", -errno);
		ret = -errno;
	}

	LOG_INF("Connected success to %s:%d", NET_CONFIG_PEER_IPV4_ADDR, NET_PORT);
	uint8_t msg[] = "Hello, world!";
	ret = send(sock, msg, strlen(msg), 0);
	if (ret < 0) {
		LOG_ERR("Failed to send UDP message %d", -errno);
	}

	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
