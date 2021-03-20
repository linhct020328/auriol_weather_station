/* 
   Auriol Weather Station Remote Decoder
   IAN: 331821_1907

   Compile: gcc -o auriol auriol.c -lgpiod

   Structure in bits: 
    0-7   = UID
    8     = Battery status? Strong(1), Weak(0)?
    9     = Scheduled(0) or Manual(1)
    10,11 = Channel (0x0 = 1, 0x1 = 2, 0x2 = 3)
    12-23 = Temperature (12-bit, signed, divide by 10 to get decimal point)
    24-27 = Unknown. Appears to be consistently 1111/0xf
    28-35 = Humidity (8-bit, signed?)
    36    = 0, End-of-message, Start of sync-bit
     
    Transmission times:
    Bit 0            = 1.5 msecs
    Bit 1            = 2.5 msecs
    Synchronise Bit  = 4.5 msecs
    Ch1 Transmission = every 59 secs
    Ch2 Transmission = every 69 secs
    Ch3 Transmission = every 79 secs
*/

#include <stdio.h>  // printf()
#include <stdint.h> // uint*_h
#include <string.h> // str manip
#include <unistd.h> // sleep(), usleep()
#include <gpiod.h>  // GPIO ops
#include <mosquitto.h>

#define GPIOPINS 22, 23, 24, 25, 18, 17, 4

enum {
    GPIOD4 = 0,
    GPIOD5,
    GPIOD6,
    GPIOD7,
    GPIOEN,
    GPIORS,
    GPIORX
};

struct var_s {
    uint8_t coda : 4; 
    uint16_t humidity : 8; // Don't ask
    uint8_t unknown : 4; 
    int16_t celcius : 12; 
    uint8_t channel : 2;
    uint8_t manual : 1;
    uint8_t charge : 1;
    uint8_t sensor : 8;
    uint32_t null : 24;
}__attribute((__packed__));

union tempdata {
    struct var_s var;
    uint64_t raw;
};

void lcd_line_set(struct gpiod_line_bulk *lines, int index, int value) {
    int ret = 0;
    struct gpiod_line *line;

    line = gpiod_line_bulk_get_line(lines, index);
    ret = gpiod_line_set_value(line, value);
    if (ret != 0) {
         fprintf(stderr, "failure setting value on line\n");
	 exit(6);
    }
}

void lcd_line_pulse(struct gpiod_line_bulk *lines) {
    lcd_line_set(lines, GPIOEN, 1);
    usleep(20);
    lcd_line_set(lines, GPIOEN, 0);
    usleep(37);
}

void lcd_set_nibble(struct gpiod_line_bulk *lines, uint8_t mode, uint8_t nibble) {
    int i, ret;

    lcd_line_set(lines, GPIORS, mode);

    for (i=0; i<5; i++) 
	lcd_line_set(lines, i, 0);

    for (i=0; i<4; i++) 
	lcd_line_set(lines, i, (nibble>>i & 1));

    // Commit nibble to the display
    lcd_line_pulse(lines);
}

void lcd_set_byte(struct gpiod_line_bulk *lines, uint8_t mode, uint8_t byte) {
    lcd_set_nibble(lines, mode, (byte>>4) & 0xf);
    lcd_set_nibble(lines, mode, (byte) & 0xf);
}

void lcd_send_msg(struct gpiod_line_bulk *lines, char *str) {
    int i;
    lcd_set_byte(lines, 0, 0x01); // Clear display
    usleep(1520);
    lcd_set_byte(lines, 0, 0x80); // Line 1 
    for (i = 0; i < strlen(str); i++) {
	// Line feed
        if ( str[i] == 0x0A ) {
            lcd_set_byte(lines, 0, 0xC0); // Line 2
	} else {
            lcd_set_byte(lines, 1, str[i]);
	}
    }
}

void lcd_init_4bit_16x2(struct gpiod_line_bulk *lines) {
    // 4 Bit setup
    lcd_set_nibble(lines, 0, 0x3);
    usleep(4100);
    lcd_set_nibble(lines, 0, 0x3);
    usleep(100);
    lcd_set_byte(lines, 0, 0x32);

    // Set up screen
    lcd_set_byte(lines, 0, 0x06); // Cursor appends (+1)
    lcd_set_byte(lines, 0, 0x0C); // Display on, Cursor off, Blink off
    lcd_set_byte(lines, 0, 0x28); // Two lines, 4 bits

    // Clear and wait
    lcd_set_byte(lines, 0, 0x01); // Clear display
    usleep(1520);
}

// Used to calculate time difference
static inline void timesecdiff(struct timespec *old, struct timespec *new,
                               struct timespec *diff)
{
    diff->tv_sec = new->tv_sec - old->tv_sec;
    diff->tv_nsec = new->tv_nsec - old->tv_nsec;
    if (diff->tv_nsec < 0) {
        diff->tv_nsec += 1000000000L;
        diff->tv_sec--;
    }
}

void cb_connect(struct mosquitto *mqtt, void *obj, int code) {
	printf("Connection status: %s\n", mosquitto_connack_string(code));
	if (code)
		mosquitto_disconnect(mqtt);
}

void cb_publish(struct mosquitto *mqtt, void *obj, int msg_id) {
 	printf("Message sent as: %u\n", msg_id);
}	

void parseprintwait(struct gpiod_line_bulk *lines, struct mosquitto *mqtt, uint64_t buf)
{
    union tempdata u;
    char msg[33];
    char mqtttopic[] = "weather/raw";
    int ret;

    buf<<=3; // pad from 37-bit to 40-bit (i.e. 8 bytes)
    u.raw = buf & 0xFFFFFFFFFF; // cast 64-bit input to 40-bit

    // Print to stdout
    printf("%ld: id=%02x,pow=%u,man=%u,ch=%u,temp=%.1f,rh=%u\n",
           time(NULL), u.var.sensor, u.var.charge, u.var.manual, 
	   u.var.channel + 1, ((float)u.var.celcius / 10), u.var.humidity);

    // Print to LCD
    sprintf(msg, "#%u Temp: %.1f\337C\nHumidity: %u%%",
	   u.var.channel + 1, ((float)u.var.celcius / 10), u.var.humidity);
    lcd_send_msg(lines, msg);

    // Print to MQTT
    sprintf(msg, "%ld: %llu", time(NULL), u.raw);
    ret = mosquitto_publish(mqtt, NULL, mqtttopic, strlen(msg), msg, 2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mqtt);
        fprintf(stderr, "Couldn't connect: %s\n", mosquitto_strerror(ret));
        exit(3);
    }

    // Wait for next transmission
    sleep((u.var.channel + 1) * 10 + 49);
}

void main(void)
{
    int gpios[] = { 22, 23, 24, 25, 18, 17, 4 };
    struct timespec timeout = { 80, 0 }; // maximum time between messages
    struct timespec timegap, last_event_time = { 0, 0 }; 
    struct gpiod_chip *chip;
    struct gpiod_line_request_config config;
    struct gpiod_line *tmpline, *rxline;
    struct gpiod_line_bulk lines;
    struct gpiod_line_event events[16];
    char mqtthost[] = "localhost";
    char mqtttopic[] = "weather/raw";
    int mqttport = 1883;
    int mqtttimeout = 60;
    struct mosquitto *mqtt;
    int i, ret, bitcount;
    uint64_t buf;

    // Tell the gpiod that we are looking for a LOW>HIGH event
    config.request_type = GPIOD_LINE_REQUEST_EVENT_RISING_EDGE;
    config.consumer = "auriol";
    config.flags = 0;

    // Open the GPIO chip
    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        fprintf(stderr, "failure opening chip\n");
        exit(1);
    }

    // Get all 7 GPIOS (1 = Rx, 6 = LCD)
    ret = gpiod_chip_get_lines(chip, gpios, 7, &lines);
    if (ret != 0) {
            fprintf(stderr, "failure getting lines from chip\n");
            exit(2);
    }

    // Rx line check
    rxline = gpiod_line_bulk_get_line(&lines, GPIORX);
    ret = gpiod_line_request(rxline, &config, 0);
    if (ret) {
        fprintf(stderr, "failure requesting line\n");
        exit(3);
    }

    // I couldn't get the bulk thing working - this does work however
    for (i=0;i<6;i++) {
        tmpline = gpiod_line_bulk_get_line(&lines, i);
        ret = gpiod_line_request_output(tmpline, "abcdefgh", 0);
        if (ret != 0) {
            fprintf(stderr, "failure requesting line for output\n");
            exit(3);
        }
    }

    mosquitto_lib_init();
    mqtt = mosquitto_new(NULL, true, NULL);
    if(mqtt == NULL) {
        fprintf(stderr, "Error initialising MQTT\n");
        exit(1);
    }

    mosquitto_connect_callback_set(mqtt, cb_connect);
    mosquitto_publish_callback_set(mqtt, cb_publish);

    ret = mosquitto_connect(mqtt, mqtthost, mqttport, mqtttimeout);

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

    // Prepare the LCD
    lcd_init_4bit_16x2(&lines);
    lcd_send_msg(&lines, "Awaiting Reading");

    // Go find a tranmission
    for(;;) {
        // Wait for a rising edge...
        ret = gpiod_line_event_wait(rxline, &timeout);
        if (ret < 0) {
            fprintf(stderr, "failure waiting for line event\n");
	    exit(4);
	}
	// No events...
        if (ret == 0)
            continue;

        // if a LOW>HIGH event occurs, record it
        ret = gpiod_line_event_read_multiple(rxline, events, 
			(sizeof(events) / sizeof(*(events))));
        if (ret < 0) {
            fprintf(stderr, "failure reading multiple line events\n");
	    exit(5);
	}

	// Cycle through the LOW>HIGH events
        for(i = 0; i < ret; i++) {
            // Calculate the time between this event and last event
            timesecdiff(&last_event_time, &events[i].ts, &timegap);
            last_event_time = events[i].ts;

            // No point doing anything if it was more than a second
            if (timegap.tv_sec == 0) {
                // Try to round up the nanosecs to the nearest 1.0 ms
		switch ((timegap.tv_nsec - 460000) / 100000) {
		case 10: 
		    // 1.0 msec == 0 bit (Keep going)
		    buf<<=1;
                    bitcount++;
                    continue;
		case 20:
		    // 2.0 msec == 1 bit (Keep going)
		    buf<<=1;
		    buf |= 1;
                    bitcount++;
                    continue;
		case 40:
		    // 4.0 msec == sync bit (EOM)
                    if (bitcount == 37)  {
                        parseprintwait(&lines, mqtt, buf);
		    }
                }
            } 
	    // Reset, start again
            bitcount = 0;
	    buf = 0;
        }
    }        

    // Clean up - should really use signals here
    gpiod_line_release_bulk(&lines);
    gpiod_chip_close(chip);
}
