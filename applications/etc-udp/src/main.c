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

#define NET_CONFIG_PEER_IPV4_ADDR "142.93.158.106"
#define NET_CONFIG_PEER_IPV6_ADDR "2604:a880:cad:d0::de9:1001"
#define NET_PORT 8080
#define NET_IPPROTO IPPROTO_DTLS_1_2

void main(void)
{
	int sock;
	int ret = 0;
	struct sockaddr_in addr4;
	struct sockaddr_in addr6;
	struct sockaddr *addr = (struct sockaddr *)&addr4;
	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(NET_PORT);
	inet_pton(AF_INET, NET_CONFIG_PEER_IPV4_ADDR, &addr4.sin_addr);

	sock = socket(addr->sa_family, SOCK_DGRAM, NET_IPPROTO);
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
	uint8_t msg[] = "Hello, world!\r\n";
	ret = send(sock, msg, strlen(msg), 0);
	if (ret < 0) {
		LOG_ERR("Failed to send UDP message %d", -errno);
	}
	k_sleep(K_SECONDS(1));

	/* Close and open new IPv6 socket. */
	ret = close(sock);
	if (ret < 0) {
		LOG_ERR("Failed to close socket %d", -errno);
	}
	
#if 0
	addr = (struct sockaddr *)&addr6;
	addr6.sin_family = AF_INET6;
	addr6.sin_port = htons(NET_PORT);
	inet_pton(AF_INET6, NET_CONFIG_PEER_IPV6_ADDR, &addr6.sin_addr);
	sock = socket(addr->sa_family, SOCK_DGRAM, NET_IPPROTO);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", -errno);
		return;
	}

	LOG_INF("Socket is ready %d", sock);
	/* Call connect so we can use send and recv. */
	ret = connect(sock, addr, sizeof(addr4));
	if (ret < 0) {
		LOG_ERR("Cannot connect to UDP v6 remote %d", -errno);
		ret = -errno;
	}

	LOG_INF("Connected success to %s:%d", NET_CONFIG_PEER_IPV6_ADDR, NET_PORT);
	uint8_t msgv6[] = "Hello, world v6!\r\n";
	ret = send(sock, msgv6, strlen(msgv6), 0);
	if (ret < 0) {
		LOG_ERR("Failed to send UDP message %d", -errno);
	}

	ret = close(sock);
	if (ret < 0) {
		LOG_ERR("Failed to close socket %d", -errno);
	}
#endif

	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
