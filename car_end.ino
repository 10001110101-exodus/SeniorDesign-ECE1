#include <Wire.h>
#include <RadioLib.h>
#include <XPowersLib.h>
#include <cstdint>
#include <ESP32-TWAI-CAN.hpp>
#include <shared_defs.h>
#include "utilities.h"



XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// car ID **** NEEDS TO BE CHANGED FOR EACH CAR AND STARTS AT 0 ****
#define MY_ID           1


CanFrame rxFrame;

// CAN data (all 2 bytes)
uint16_t curr_time = 0;
uint16_t dischEn = 0;
uint16_t pckVolt = 0;
uint16_t pckCurr = 0;
uint16_t pckTemp = 0;
uint16_t stateChar = 0;
uint16_t minCellVolt = 0;
uint16_t lvIn = 0;
uint16_t torFb = 0;
uint16_t RPM = 0;
uint16_t fluxFb = 0;
uint16_t inlineAcc = 0;
uint16_t latAcc = 0;
uint16_t vertAcc = 0;
uint16_t rollRate = 0;
uint16_t pitchRate = 0;
uint16_t yawRate = 0;



// make a packet given the abp and the actual data
static void make_packet(uint16_t sender_id, uint16_t abp, const uint16_t data[DATA_BYTES], uint16_t out_packet[PCK_LEN]) {
    out_packet[0] = (uint16_t)(sender_id & 0xFFFF);
    out_packet[1] = (uint16_t)(abp & 0xFFFF);
    memcpy(out_packet + 2, data, DATA_BYTES);
}

// wait for ACK (the dest_id, abp num, status) for the expected abp num
// return 1 if received, 0 if timeout
static int wait_for_ack(uint16_t expected_abp, uint8_t *status_out) {
    uint32_t start = millis();      // start recording the time
    uint8_t ack[ACK_LEN];

    while ( ( millis() - start ) < ACK_TIMEOUT_MS ) {
        int16_t st = radio.receive(ack, ACK_LEN, 100);

        // check that it was received properly
        if (st == RADIOLIB_ERR_NONE) {
            // ignore acks that are not intended for this transmitter
            if (ack[0] != MY_ID) {
                continue;
            }
            // make sure it's the right abp
            if (ack[1] == (expected_abp & 0xFFFF)) {
                // set the status of the ack
                *status_out = ack[2];
                return 1;
            }
        // otherwise wrong sequence so ignore
        }
        // keep waiting until timeout
        else if (st == RADIOLIB_ERR_RX_TIMEOUT) {}
        else {}
    }
    // failed so return 0
    return 0;

}


// send data with retries if not successful
// return 1 if delivered else 0
static int send_with_retries(uint16_t abp, const uint16_t packet[PCK_LEN]) {

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++ ) {
        int16_t st = radio.transmit((uint8_t*)packet, PCK_LEN);

        // check if successfully sent
        if (st != RADIOLIB_ERR_NONE) {
            // a transmit error occured
            Serial.printf("ABP=%u transmit error %d (attempt %u/%u)\n", abp, st, attempt, MAX_RETRIES);
        }
        else {
            // successfully transmitted
            Serial.printf("Sent ABP=%u attempt %u/%u\n", abp, attempt, MAX_RETRIES);
        }

        // wait for ack
        uint8_t ack_status = 0xFF;
        int got_ack = wait_for_ack(abp, &ack_status);
        if (!got_ack) {
            // timeout
            Serial.printf("ABP=%u ACK timeout -> retry %u/%u\n", abp, attempt, MAX_RETRIES);
            continue;
        }

        // otherwise we got an ack so check the status
        if (ack_status == 0) {
            Serial.printf("Sent SUCCESSFULLY ABP=%u\n", abp);
            return 1;
        }

        else if (ack_status == 1) {
            Serial.printf("ABP=%u receiver says DUPLICATE\n", abp);
            return 1;
        }

    }
    // otherwise failed within all these attempts
    Serial.printf("ABP=%u FAILED after %u retries\n", abp, MAX_RETRIES);
    return 0;
}



// handles all the t-beam power and radio nonsense
void power_up_tbeam() {
    Serial.begin(115200);
    
    // power chip config
    Wire.begin(I2C_SDA, I2C_SCL);
    PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    PMU.setALDO2Voltage(3300); // lora power
    PMU.enableALDO2();
    PMU.setALDO3Voltage(1800); // clock power
    PMU.enableALDO3();
    PMU.setDLDO1Voltage(3300); // rf switch power
    PMU.enableDLDO1();

    // radio config
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    // 915.0 frequency, 125.0 bandwidth, 1.8v tcxo
    int state = radio.begin(915.0, 125.0, 7, 7, 0x12, 17, 8, 1.8, true);
    
    if (state == RADIOLIB_ERR_NONE) {
        radio.setTCXO(1.8); 
        radio.setDio2AsRfSwitch();
        Serial.println("transmitter ready");
    } else {
        Serial.println("radio failed");
        while(1);
    }
}


void setup() {
    power_up_tbeam();
    delay(200);
    pinMode(USER_BUTTON, INPUT_PULLUP);

    // CAN interface setup
    ESP32Can.setPins(CAN_TX_PIN, CAN_RX_PIN); // configure CAN pins
    ESP32Can.setSpeed(TWAI_SPEED_1000KBPS); // set CAN speed to 1Mbps
  
    ESP32Can.setRxQueueSize(5);
    if (ESP32Can.begin())
    Serial.println("CAN set up");

    pinMode(BOARD_LED, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);
}

static uint32_t counter = 0;

void loop() {

    // grabbing data from CAN
    // if(ESP32Can.readFrame(rxFrame, 1000)) {
    //     // Comment out if too many frames
    //     Serial.printf("Received frame: %03X  \r\n", rxFrame.identifier);
    //     if(rxFrame.identifier == 0x600)
    //     {
    //       RPM = rxFrame.data[0] << 8 | rxFrame.data[1];
    //       vSpeed = rxFrame.data[2] << 8 | rxFrame.data[3];
    //       dSpeed = rxFrame.data[4] << 8 | rxFrame.data[5];
    //       wSP = rxFrame.data[6] << 8 | rxFrame.data[7]; 
    //     }
    //     else if(rxFrame.identifier == 0x601)
    //     {
    //       frSP = rxFrame.data[0] << 8 | rxFrame.data[1];
    //       flSP = rxFrame.data[2] << 8 | rxFrame.data[3];
    //       rlSP = rxFrame.data[4] << 8 | rxFrame.data[5];
    //       rrSP = rxFrame.data[6] << 8 | rxFrame.data[7];
    //     }
    // }

    static uint16_t abp = 0;
    static uint16_t sender_id = MY_ID;


    uint16_t packet[PCK_LEN];

    // putting CAN data into a payload
    uint16_t payload[DATA_BYTES] = {
        curr_time, dischEn, pckVolt, pckCurr, pckTemp, 
        stateChar, minCellVolt, lvIn, torFb, RPM,
        fluxFb, inlineAcc, latAcc, vertAcc, rollRate,
        pitchRate, yawRate };

    make_packet(sender_id, abp, payload, packet);

    Serial.printf("\nSending frame counter=%lu seq=%u\n", (unsigned long)counter, abp);

    int ok = send_with_retries(abp, packet);
    if (ok) {
        counter++;
        abp ^= 1; 
    } else {
        Serial.println("Giving up on this frame (will try again next loop)");
    }

    delay(1000);
}
