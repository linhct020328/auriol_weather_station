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
#include <unistd.h> // sleep(), usleep()
#include <gpiod.h>  // GPIO ops

#define BCMGPIO 4

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

void parseandwait(uint64_t buf)
{
    union tempdata u;
    buf<<=3; // pad from 37-bit to 40-bit (i.e. 8 bytes)
    printf("raw data: 0x%16llx\n", buf);
    u.raw = buf & 0xFFFFFFFFFF; // cast 64-bit input to 40-bit

    printf("%ld: id=%02x,pow=%u,man=%u,ch=%u,temp=%.1f,rh=%u\n",
           time(NULL), u.var.sensor, u.var.charge, u.var.manual, 
	   u.var.channel + 1, ((float)u.var.celcius / 10), u.var.humidity);

    printf("Temp: %.1f\337C\nHumidity: %u%\n",
	   ((float)u.var.celcius / 10), u.var.humidity);

    sleep((u.var.channel + 1) * 10 + 49);
}

void main(void)
{
    int bcmgpio = 4;
    struct timespec timeout = { 80, 0 }; // maximum time between messages
    struct timespec timegap, last_event_time = { 0, 0 }; 
    struct gpiod_chip *chip;
    struct gpiod_line_request_config config;
    struct gpiod_line *line;
    struct gpiod_line_event events[16];
    int i, ret, bitcount;
    uint64_t buf;

    // Tell the gpiod that we are looking for a LOW>HIGH event
    config.request_type = GPIOD_LINE_REQUEST_EVENT_RISING_EDGE;
    config.consumer = "auriol";
    config.flags = 0;

    // Open the GPIO chip and line for access
    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip)
        printf("Unable to open chip\n");

    line = gpiod_chip_get_line(chip, bcmgpio);
    if (!line)
        printf("Unable to get GPIO line on chip\n");

    ret = gpiod_line_request(line, &config, 0);
    if (ret)
        printf("Unable to request GPIO line");
    
    for(;;) {
        // Wait for a rising edge...
        ret = gpiod_line_event_wait(line, &timeout);
        if (ret < 0)
            printf("Unable to wait for event on GPIO");
        if (ret == 0)
            continue;

        // if a LOW>HIGH event occurs, record it
        ret = gpiod_line_event_read_multiple(line, events, 
			(sizeof(events) / sizeof(*(events))));
        if (ret < 0)
            printf("Unable to read event on GPIO");

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
                    if (bitcount == 37) 
                        parseandwait(buf);
                }
            } 
	    // Reset, start again
            bitcount = 0;
	    buf = 0;
        }
    }        

    // Clean up - should really use signals here
    gpiod_line_release(line);
    gpiod_chip_close(chip);
}
