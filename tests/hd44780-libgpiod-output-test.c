#include <gpiod.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void line_set(struct gpiod_line_bulk *lines, int index, int value) {
    int ret = 0;
    struct gpiod_line *line;

    line = gpiod_line_bulk_get_line(lines, index);
    ret = gpiod_line_set_value(line, value);
    if (ret != 0)
         printf("Error setting line\n");
}

void line_pulse(struct gpiod_line_bulk *lines, int index) {
    line_set(lines, 4, 1);
    usleep(20);
    line_set(lines, 4, 0);
    usleep(37);
}

void set_nibble(struct gpiod_line_bulk *lines, uint8_t mode, uint8_t nibble) {
    int i, ret;

    line_set(lines, 5, mode);

    for (i=0; i<5; i++) 
	line_set(lines, i, 0);

    for (i=0; i<4; i++) 
	line_set(lines, i, (nibble>>i & 1));

    // Commit nibble to the display
    line_pulse(lines, 4);
}

void set_byte(struct gpiod_line_bulk *lines, uint8_t mode, uint8_t byte) {
    set_nibble(lines, mode, (byte>>4) & 0xf);
    set_nibble(lines, mode, (byte) & 0xf);
}

void send_msg(struct gpiod_line_bulk *lines, char *str) {
    int i;
    set_byte(lines, 0, 0x80); // Line 1 (optional)
    for (i = 0; i < strlen(str); i++) {
	// Line feed
        if ( str[i] == 0x0A ) {
            set_byte(lines, 0, 0xC0); // Line 2
	} else {
            set_byte(lines, 1, str[i]);
	}
    }
}

void main (void) {
    int ret, i;
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    struct gpiod_line_bulk lines;
    struct gpiod_line_request_config config;
    // GPIO D4, D5, D6, D7, E, RS
    int gpios[] = { 22, 23, 24, 25, 18, 17 };

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip)
        printf("Unable to open chip\n");
    
    ret = gpiod_chip_get_lines(chip, gpios, 6, &lines);
    if (ret != 0)
	    printf("failure getting lines from chip\n");

    // This needs to be replaced - not sure why the bulk one doesn't work!
    for (i=0;i<6;i++) {
        line = gpiod_line_bulk_get_line(&lines, i);
        ret = gpiod_line_request_output(line, "abcdefgh", 0);
        if (ret != 0)
            printf("Error setting line\n");
    }

    // 4 Bit setup
    set_nibble(&lines, 0, 0x3);
    usleep(4100);
    set_nibble(&lines, 0, 0x3);
    usleep(100);
    set_byte(&lines, 0, 0x32);

    // Set up screen
    set_byte(&lines, 0, 0x06); // Cursor movement
    set_byte(&lines, 0, 0x0C); // Display, Cursor, Blink
    set_byte(&lines, 0, 0x28); // Screen dimensions
    set_byte(&lines, 0, 0x01); // Clear
    usleep(1520);

    // Send Text
    send_msg(&lines, "Temperature: 9\337C\nRl Humidity: 86%%"); // :

    sleep(1);
    gpiod_line_release_bulk(&lines);
    gpiod_chip_close(chip);
}
