#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void cb_connect(struct mosquitto *mqtt, void *obj, int code) {
	printf("Connection status: %s\n", mosquitto_connack_string(code));
	if (code)
		mosquitto_disconnect(mqtt);
}

void cb_publish(struct mosquitto *mqtt, void *obj, int msg_id) {
 	printf("Message sent as: %u\n", msg_id);
}	

void main(void) {
	struct mosquitto *mqtt;
	int ret;
	char msg[] = "12345678";
	char top[] = "weather/raw";

	char host[] = "localhost";
	int port = 1883;
	int timeout = 60;

	mosquitto_lib_init();
	mqtt = mosquitto_new(NULL, true, NULL);
	if(mqtt == NULL) {
		fprintf(stderr, "Error initialising MQTT\n");
		exit(1);
	}

	mosquitto_connect_callback_set(mqtt, cb_connect);
	mosquitto_publish_callback_set(mqtt, cb_publish);

	ret = mosquitto_connect(mqtt, host, port, timeout);

	if (ret != MOSQ_ERR_SUCCESS) {
		mosquitto_destroy(mqtt);
		fprintf(stderr, "Couldn't connect: %s\n", mosquitto_strerror(ret));
		exit(2);
	}

	ret = mosquitto_loop_start(mqtt);
	if (ret != MOSQ_ERR_SUCCESS) {
		mosquitto_destroy(mqtt);
		fprintf(stderr, "Couldn't connect: %s\n", mosquitto_strerror(ret));
		exit(2);
	}

	for(;;) {
		ret = mosquitto_publish(mqtt, NULL, top, strlen(msg), msg, 2, false);
		if (ret != MOSQ_ERR_SUCCESS) {
			mosquitto_destroy(mqtt);
			fprintf(stderr, "Couldn't connect: %s\n", mosquitto_strerror(ret));
			exit(3);
		}
		sleep(10);
	}
}
