#include <ESP32-TWAI-CAN.hpp>
#include "utilities.h"

#define CAN_TX_PIN 13
#define CAN_RX_PIN 14

uint16_t RPM = 0;
uint16_t vSpeed = 0;
uint16_t dSpeed = 0;
uint16_t wSP = 0;
uint16_t frSP = 0;
uint16_t flSP = 0;
uint16_t rrSP = 0;
uint16_t rlSP = 0; 

CanFrame rxFrame;
void setup() {
  // put your setup code here, to run once:
  ESP32Can.setPins(CAN_TX_PIN, CAN_RX_PIN); // configure CAN pins
  ESP32Can.setSpeed(TWAI_SPEED_1000KBPS); // set CAN speed to 1Mbps
  
      ESP32Can.setRxQueueSize(5);
  if (ESP32Can.begin())
    Serial.println("CAN set up");

  pinMode(BOARD_LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
}


void loop() {
  // put your main code here, to run repeatedly:W
  if(ESP32Can.readFrame(rxFrame, 1000)) {
        // Comment out if too many frames
        Serial.printf("Received frame: %03X  \r\n", rxFrame.identifier);
        if(rxFrame.identifier == 0x600)
        {
          RPM = rxFrame.data[0] << 8 | rxFrame.data[1];
          vSpeed = rxFrame.data[2] << 8 | rxFrame.data[3];
          dSpeed = rxFrame.data[4] << 8 | rxFrame.data[5];
          wSP = rxFrame.data[6] << 8 | rxFrame.data[7]; 
        }
        else if(rxFrame.identifier == 0x601)
        {
          frSP = rxFrame.data[0] << 8 | rxFrame.data[1];
          flSP = rxFrame.data[2] << 8 | rxFrame.data[3];
          flSP = rxFrame.data[4] << 8 | rxFrame.data[5];
          rrSP = rxFrame.data[6] << 8 | rxFrame.data[7];
        }
    }
}
