#ifndef __CSV_TO_ARDUINO_H__
#define __CSV_TO_ARDUINO_H__
#define SERIAL_PORT ttyACM0
#define BAUD B115200
#define BUFFER_SIZE 256

int serialPortFlush(int fd);

#endif