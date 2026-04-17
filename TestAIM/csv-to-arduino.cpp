
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "serial-to-can.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>

char *portname = "ttyACM0";
char oldbuf[BUFFER_SIZE] = "temp";

int main(int argc, char* argv[])
{
    int fd;
    // open file descriptor in non-blocking mode
    fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1)
    {
        perror("open_port: Unable to open /dev/ttyACMO ");
        return -1;
    }
    printf("fd opened\n");

    usleep(3500000); // arduino reboot
    //printf("\n");
    serialPortFlush(fd);
    // set up control struct 
    struct termios toptions;

    // get currently set options for the tty
    tcgetattr(fd, &toptions);

    // set to 115200 baud rate
    cfsetispeed(&toptions, BAUD);
    cfsetospeed(&toptions, BAUD);
    // 8 bit no parity no stop bits
    toptions.c_cflag &= ~PARENB;
    toptions.c_cflag &= ~CSTOPB;
    toptions.c_cflag &= ~CSIZE;
    toptions.c_cflag |= CS8;
    // cannonical mode
    toptions.c_lflag |= ICANON;
    // commit the serial port settings
    tcsetattr(fd, TCSANOW, &toptions);
    /*
        0 -> time
        1 -> bms disch en
        2 -> pack voltage
        3 -> pack current 
        4 -> pack temp
        5 -> state of charge
        6 -> min cell voltage
        7 -> bms lv ibnput
        8 -> powertrain
        9 -> torque feedback
        10 -> rpm 
        11 -> flux feeback  
        12 -> inline acc
        13 -> lat acc
        14 ->  vert acc
        15 -> roll rate 
        16 -> pitch rate 
        17 -> yawrate  
    */
    
    std::ifstream src("./data1.csv");
    if(!src)
    {
        perror("failed to open data file");
        return -1;
    }
    uint16_t id = 0x600;
    std::string line, word;
    std::getline(src, line); // skip first 
    while(std::getline(src, line))
    {
        int i = 0;
        id = 0x600;

        std::stringstream s(line);
        std::string word;
        unsigned char packet[6];
        std::memcpy(packet, &id, 2 * sizeof(unsigned char));
        while(getline(s, word, ',')){
            uint16_t temp;
            if(i == 0)
            {
                float tmp = std::stof(word);
                temp = tmp * 100;
            }
            else
                temp = std::stoi(word);
            if((i%2) == 0)
            {
                std::memcpy(packet + 2, &temp, 2 * sizeof(unsigned char));
                
            }
            else
            {
                std::memcpy(packet + 4, &temp, 2 * sizeof(unsigned char));
                
                for (int i = 0; i < 6; ++i) {
                    printf("%x ", packet[i]);
                }
                int v = write(fd, packet, 6);
                int yes = 1;
                while(yes)
                {
                    int n = read(fd, oldbuf, 64);
                    // insert terminating zero in the string 
                    oldbuf[n] = 0;
                    if(n != -1 && n!= 0)
                    {
                        std::cout << "from arduino: " << oldbuf << std::endl;
                        yes = 0;
                    }
                }
                id += 1;
                std::cout << "i: " << i/2 << std::endl;
                std::cout << "CAN ID: " << std::hex << id << std::endl;
                std::memcpy(packet, &id, 2 * sizeof(unsigned char));
                usleep(500000);
            }
            i++;
            std::cout << std::endl;
        }
    }
}

int serialPortFlush(int fd)
{
    sleep(2); 
    return tcflush(fd, TCIOFLUSH);
}