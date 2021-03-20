#include <stdio.h>
#include <gpiod.h>
#include <string.h>

// gcc -o gpiod-detect-rising gpiod-detect-rising.c -lgpiod
static inline void timediff(struct timespec *old, struct timespec *new,
                            struct timespec *diff)
{
    diff->tv_sec = new->tv_sec - old->tv_sec;
    diff->tv_nsec = new->tv_nsec - old->tv_nsec;
    if (diff->tv_nsec < 0) {
        diff->tv_nsec += 1000000000L;
        diff->tv_sec--;
    }
}

void main(void)
{
    struct timespec timeout = { 10, 0 };
    struct timespec last_time, gap_time;
    struct gpiod_chip *chip;
    struct gpiod_line_request_config config;
    struct gpiod_line *line;
    struct gpiod_line_event events[16];
    int ret, i;

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip)
        printf("unable to open chip\n");

    line = gpiod_chip_get_line(chip, 4);
    if (!line)
        printf("unable to get line\n");

    config.request_type = GPIOD_LINE_REQUEST_EVENT_RISING_EDGE;
    config.consumer = "test";
    config.flags = 0;

    ret = gpiod_line_request(line, &config, 0);
    if (ret)
        printf("unable to request line for events");
	
    for(;;) {
        memset(line, 0, sizeof(struct gpiod_line *));

        ret = gpiod_line_event_wait(line, &timeout);
        if (ret < 0)
            printf("error waiting");
        if (ret == 0)
            continue;

        int i = 0;
        ret = gpiod_line_event_read_multiple(line, events, 
                                       (sizeof(events) / sizeof(*(events))));
	if (ret < 0)
            printf("error reading event");
        for(i = 0; i < ret; i++) {
            timediff(&last_time, &events[i].ts, &gap_time);
            printf("%8ld.%09ld (%2ld.%09ld) %u\n", 
                events[i].ts.tv_sec, events[i].ts.tv_nsec,
                gap_time.tv_sec, gap_time.tv_nsec, i);
            last_time = events[i].ts;
        }
    }		

    gpiod_line_release(line);
    gpiod_chip_close(chip);
}
