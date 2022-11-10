#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <app_version.h>
#include <drivers/hwinfo.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/socket.h>
#include <errno.h>
#include <zephyr/net/tls_credentials.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

#define NET_CONFIG_PEER_IPV4_ADDR "142.93.158.106"
#define NET_CONFIG_PEER_IPV6_ADDR "2604:a880:cad:d0::de9:1001"
#define NET_PORT 8080
#define NET_IPPROTO IPPROTO_DTLS_1_2

struct pollfd sock_fds[1];


static int socket_recv_message(int sock_fd)
{
	static uint8_t in_buf[NET_IPV6_MTU];
	socklen_t from_addr_len;
	ssize_t len;
	static struct sockaddr from_addr;

	from_addr_len = sizeof(from_addr);
	len = recvfrom(sock_fd, in_buf, sizeof(in_buf) - 1, 0, &from_addr,
		       &from_addr_len);

	if (len < 0) {
		LOG_ERR("Error reading response: %d", errno);
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return -errno;
		}
		return -errno;
	}

	if (len == 0) {
		LOG_ERR("Zero length recv");
		return 0;
	}

	LOG_DBG("received %d bytes", len);
	//LOG_HEXDUMP_DBG(in_buf, len, "SOCK_RECV");

	return 0;
}

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
	
#if CONFIG_NATIVE_TLS
	int tls_native = 1;
	setsockopt(sock, SOL_TLS, TLS_NATIVE, &tls_native, sizeof(tls_native));

	static unsigned char client_psk[
				(sizeof(CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY) - 1) / 2];
	static const char client_psk_id[] = CONFIG_MODEM_QUECTEL_BG95_M3_PSK_ID;
	hex2bin(CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY, 
			sizeof(CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY) - 1,
			client_psk, sizeof(client_psk));

	tls_credential_add(1, TLS_CREDENTIAL_PSK, client_psk,
					sizeof(client_psk));
	tls_credential_add(1, TLS_CREDENTIAL_PSK_ID, client_psk_id,
					sizeof(client_psk_id) - 1);
#endif


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


	sock_fds[0].fd = sock;
	sock_fds[0].events = POLLIN;
	while (1) {
		ret = poll(sock_fds, 1, 5000);
		if (ret < 0) {
			LOG_ERR("Error in poll:%d", errno);
			errno = 0;
			k_sleep(K_SECONDS(1));
			continue;
		}

		if ((sock_fds[0].revents & POLLERR) || (sock_fds[0].revents & POLLNVAL) ||
			(sock_fds[0].revents & POLLHUP)) {
			LOG_ERR("Poll reported a socket error, %02x.", sock_fds[0].revents);
			continue;
		}

		if (sock_fds[0].revents & POLLIN) {
			while (sock) {
				ret = socket_recv_message(sock);
				if (ret) {
					break;
				}
			}
		}
	}


	/* Close socket. */
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
