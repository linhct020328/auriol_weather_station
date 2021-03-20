Some sample code for interfacing with a LIDL Auriol Weather Station.

Features:
- RF 433MHz reception example
- TX to a Hitachi 16x2 LCD
- Publish to MQTT

Uses the following libraries of note:
- libgpiod (supercedes wiringpi, pigpio due to kernel support)
- mosquitto (for MQTT)
