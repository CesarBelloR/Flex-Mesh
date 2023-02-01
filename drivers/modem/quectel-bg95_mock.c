#include "modem_api.h"

char* quectel_bg95_get_imei(void) {
	return "123456789";
}
char* quectel_bg95_get_revision(void) {
	return "0.0.1";
}

char* quectel_bg95_get_sim_number(void) {
	return "N.A";
}

bool quectel_bg95_is_ready(void) {
	return true;
}

int quectel_bg95_get_time(char* time_buf)
{
	*time_buf = "";
	return 0;
}

int quectel_bg95_get_rssi(void)
{
	return -50;
}