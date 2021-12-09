#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>  
#include <sys/ioctl.h>
#include <unistd.h>

#include <MQTTClient.h>
#include <cjson/cJSON.h>

static volatile sig_atomic_t g_stop = 0;

void sig_handler(int signum)
{
	(void) signum;
	g_stop = 1;
}

int switch_write(int fd, uint8_t const* buf, size_t len)
{
	int retval = 0;
	retval = write(fd, buf, len);
	if (retval != len) {
		return -1;
	}
	return 0;
}

int switch_set_max_angle(int fd)
{
	uint8_t buf[3] = {0x03, 0x00, 179};
	return switch_write(fd, buf, 3);
}

int switch_set_mode(int fd)
{
	uint8_t buf[3] = {0x01, 0x00, 0x00};
	return switch_write(fd, buf, 3);
}

int switch_enable_channels(int fd, uint8_t mask)
{
	uint8_t buf[3] = {0x02, 0x00, 0x03 & mask};
	return switch_write(fd, buf, 3);
}

int switch_reset(int fd)
{
	int retval = 0;
	retval = switch_enable_channels(fd, 0x0);
	if (-1 == retval) {
		return retval;
	}
	retval = switch_set_mode(fd);
	if (-1 == retval) {
		return retval;
	}
	retval = switch_set_max_angle(fd);
	if (-1 == retval) {
		return retval;
	}
	return 0;
}

int main(int argc, char **argv)
{
	signal(SIGINT, sig_handler);

	char const dev[] = "/dev/i2c-1";
	uint8_t addr = 0x47;
	int retval = 0;

	int fd = open(dev, O_RDWR);
	if (fd == -1) {
		perror("i2c dev open");  
		goto exit;
	}

	retval = ioctl(fd, I2C_SLAVE, addr);
	if (retval == -1) {
		perror("i2c addr set");
		goto exit_close;
	}

	retval = switch_reset(fd);
	if (-1 == retval) {
		perror("switch reset");
		goto exit_close;
	}

#if 0
	retval = switch_enable_channels(fd, 0x1);
	if (-1 == retval) {
		perror("switch enable channels");
		goto exit_close;
	}
#endif

	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	MQTTClient_create(&client, "127.0.0.1:1883", "switchd",
			MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;
	retval = MQTTClient_connect(client, &conn_opts);
	if (retval != MQTTCLIENT_SUCCESS) {
		fprintf(stderr, "mqtt connect failed\n");
		goto exit_close;
	}

	MQTTClient_subscribe(client, "zigbee2mqtt/switch0", 0);

	char* topic_name = NULL;
	int topic_length = 0;
	MQTTClient_message* message = NULL;

	int channel_mask = 0x0;

	while (!g_stop) {
		MQTTClient_receive(client, &topic_name, &topic_length, &message, 200);
		if (message != NULL) {
			cJSON *json = cJSON_ParseWithLength(
					message->payload, message->payloadlen);
			cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
			if (cJSON_IsString(action) && (action->valuestring != NULL)
					&& !strcmp(action->valuestring, "single")) {

				channel_mask ^= 0x1;
				retval = switch_enable_channels(fd, channel_mask);
				if (-1 == retval) {
					perror("switch enable channels");
				}
			}

			MQTTClient_free(topic_name);
			MQTTClient_freeMessage(&message);
		}
	}

	retval = switch_enable_channels(fd, 0x0);
	if (retval == -1) {
		perror("switch disable channels");  
	}

	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);

exit_close:
	retval = close(fd);
	if (retval == -1) {
		perror("i2c dev close");  
	}

exit:
	return retval;
}
