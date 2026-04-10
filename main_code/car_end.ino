#include <Wire.h>
#include <RadioLib.h>
#include <XPowersLib.h>
#include <cstdint>
#include <ESP32-TWAI-CAN.hpp>
#include "utilities.h"
#include <shared_defs.h>

XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// car ID **** NEEDS TO BE CHANGED FOR EACH CAR AND STARTS AT 0 ****
#define MY_ID           0


// CAN IDs
#define TIME_ID 0x600
#define BMS_DISCH_EN_ID 0x600
#define PACK_VOLT_ID 0x601
#define PACK_CURR_ID 0x601
#define PACK_TEMP_ID 0x602
#define STATE_CHRG_ID 0x602
#define MIN_CELL_VOLT_ID 0x603
#define BMS_LV_ID 0x603
#define PWRTRAIN_ID 0x604
#define TORQUE_ID 0x604
#define RPM_ID 0x605
#define FLUX_ID 0x605
#define INLINE_ID 0x606
#define LAT_ID 0x606
#define VERT_ID 0x607
#define ROLL_ID 0X607
#define PITCH_ID 0x608
#define YAW_ID 0x608

CanFrame rxFrame;

// CAN data --> Ex: Time = 0x1001 --> Time1 = 0x10, Time0 = 0x01
uint8_t Time0 = 0x00;
uint8_t BMS_Disch_Enable0 = 0x00;
uint8_t Pack_Voltage0 = 0x01;
uint8_t Pack_Current0 = 0x00;
uint8_t Pack_Temp0 = 0x00;
uint8_t State_of_Charge0 = 0x00;
uint8_t Min_Cell_Voltage0 = 0x00;  
uint8_t BMS_LV_Input0 = 0x00;
uint8_t Torque_Feedback0 = 0x00; 
uint8_t RPM0 = 0x00;
uint8_t Flux_Feedback0 = 0x00;
uint8_t InlineAcc0 = 0x00;
uint8_t LateralAcc0 = 0x00; 
uint8_t VerticalAcc0 = 0x00;
uint8_t RollRate0 = 0x00; 
uint8_t PitchRate0 = 0x00;
uint8_t YawRate0 = 0x00;

uint8_t Time1 = 0x00;
uint8_t BMS_Disch_Enable1 = 0x00;
uint8_t Pack_Voltage1 = 0x10;
uint8_t Pack_Current1 = 0x00;
uint8_t Pack_Temp1 = 0x00;
uint8_t State_of_Charge1 = 0x00;
uint8_t Min_Cell_Voltage1 = 0x00; 
uint8_t BMS_LV_Input1 = 0x00;
uint8_t Torque_Feedback1 = 0x00; 
uint8_t RPM1 = 0x00;
uint8_t Flux_Feedback1 = 0x00;
uint8_t InlineAcc1 = 0x00;
uint8_t LateralAcc1 = 0x00; 
uint8_t VerticalAcc1 = 0x00;
uint8_t RollRate1 = 0x00; 
uint8_t PitchRate1 = 0x00;
uint8_t YawRate1 = 0x00;


// make a packet given the abp and the actual data
static void make_packet(uint8_t sender_id, uint8_t abp, const uint8_t data[DATA_BYTES], uint8_t out_packet[DATA_PCK_LEN]) {
    out_packet[0] = (uint8_t)(sender_id & 0xFF);
    out_packet[1] = (uint8_t)(abp & 0xFF);
    memcpy(out_packet + 2, data, DATA_BYTES);
}

// wait for ACK (the dest_id, abp num, status) for the expected abp num
// return 1 if received, 0 if timeout
static int wait_for_ack(uint8_t expected_abp, uint8_t *status_out) {
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
            if (ack[1] == (expected_abp & 0xFF)) {
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
static int send_with_retries(uint8_t abp, const uint8_t packet[DATA_PCK_LEN]) {

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++ ) {
        int16_t st = radio.transmit((uint8_t*)packet, DATA_PCK_LEN);

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
    
    // setting each byte of data from CAN into variables 
    if(ESP32Can.readFrame(rxFrame,1000))
    {
      Serial.printf("Received frame: %03X   \r\n", rxFrame.identifier);
      switch(rxFrame.identifier)
      {
        case 0x600:
          Time0 = rxFrame.data[0]; 
          Time1 = rxFrame.data[1];
          BMS_Disch_Enable0 = rxFrame.data[2];
          BMS_Disch_Enable1 = rxFrame.data[3];
          break;
        case 0x601:
          Pack_Voltage0 = rxFrame.data[0];
          Pack_Voltage1 = rxFrame.data[1];
          Pack_Current0 = rxFrame.data[2];
          Pack_Current0 = rxFrame.data[3];
          break;
        case 0x602:
          Pack_Temp0 = rxFrame.data[0];
          Pack_Temp1 = rxFrame.data[1];
          State_of_Charge0 = rxFrame.data[2];
          State_of_Charge1 = rxFrame.data[3];
          break;
        case 0x603:
          Min_Cell_Voltage0 = rxFrame.data[0];
          Min_Cell_Voltage1 = rxFrame.data[1];
          BMS_LV_Input0 = rxFrame.data[2];
          BMS_LV_Input1 = rxFrame.data[3];
          break;
        case 0x604:
          Torque_Feedback0 = rxFrame.data[0];
          Torque_Feedback1 = rxFrame.data[1];
          RPM0 = rxFrame.data[2];
          RPM1 = rxFrame.data[3];
          break;
        case 0x605:
          Flux_Feedback0 = rxFrame.data[0];
          Flux_Feedback1 = rxFrame.data[1];
          InlineAcc0 = rxFrame.data[2];
          InlineAcc1 = rxFrame.data[3];
          break;
        case 0x606:
          LateralAcc0 = rxFrame.data[0];
          LateralAcc1 = rxFrame.data[1];
          VerticalAcc0 = rxFrame.data[2];
          VerticalAcc1 = rxFrame.data[3];
          break;
        case 0x607:
          RollRate0 = rxFrame.data[0];
          RollRate1 = rxFrame.data[1];
          PitchRate0 = rxFrame.data[2];
          PitchRate1 = rxFrame.data[3];
          break;
        case 0x608:
          YawRate0 = rxFrame.data[0];
          YawRate1 = rxFrame.data[1];
          break;
      }
    }


    // initialize abp, sender_id, and the overall packet that will be sent
    static uint8_t abp = 0;
    static uint8_t sender_id = MY_ID;
    uint8_t packet[DATA_PCK_LEN];

    // CAN data is put into array called payload
    uint8_t payload[DATA_BYTES] = {
        Time0, Time1,
        BMS_Disch_Enable0, BMS_Disch_Enable1,
        Pack_Voltage0, Pack_Voltage1,
        Pack_Current0, Pack_Current1,
        Pack_Temp0, Pack_Temp1,
        State_of_Charge0, State_of_Charge1,
        Min_Cell_Voltage0, Min_Cell_Voltage1, 
        BMS_LV_Input0, BMS_LV_Input1,
        Torque_Feedback0, Torque_Feedback1,
        RPM0, RPM1,
        Flux_Feedback0, Flux_Feedback1,
        InlineAcc0, InlineAcc1,
        LateralAcc0, LateralAcc1,
        VerticalAcc0, VerticalAcc1,
        RollRate0, RollRate1,
        PitchRate0, PitchRate1,
        YawRate0, YawRate1
    };

    // make the packet with the id, abp, and payload
    // put into array packet
    make_packet(sender_id, abp, payload, packet);

    Serial.printf("\nSending frame counter=%lu seq=%u\n", (unsigned long)counter, abp);

    // send the packet 
    int ok = send_with_retries(abp, packet);
    if (ok) {
        counter++;
        abp ^= 1;           // alternate the bit 
    } else {
        Serial.println("Giving up on this frame (will try again next loop)");
    }
}
