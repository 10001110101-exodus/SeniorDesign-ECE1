#include <RadioLib.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <shared_defs.h>

XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);


// array index is the sender_id
static int abp_array[NUM_CARS];


// send an acknowledgement
static void send_ack(uint8_t sender_id, uint8_t abp, uint8_t status) {
    // create a 3 byte acknowledgement and send
    uint8_t ack[ACK_LEN];
    ack[0] = (uint8_t)(sender_id & 0xFF);
    ack[1] = (uint8_t)(abp & 0xFF);
    ack[2] = status;
    radio.transmit(ack, ACK_LEN);
}


void power_up_tbeam() {
    Serial.begin(115200);
    
    Wire.begin(I2C_SDA, I2C_SCL);
    PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    PMU.setALDO2Voltage(3300);
    PMU.enableALDO2();
    PMU.setALDO3Voltage(1800);
    PMU.enableALDO3();
    PMU.setDLDO1Voltage(3300);
    PMU.enableDLDO1();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = radio.begin(915.0, 125.0, 7, 7, 0x12, 17, 8, 1.8, true);
    
    if (state == RADIOLIB_ERR_NONE) {
        radio.setTCXO(1.8); 
        radio.setDio2AsRfSwitch();
        radio.startReceive(); // start listening immediately
        Serial.println("receiver ready");
    } else {
        Serial.println("radio failed");
        while(1);
    }
}


void setup() {
    power_up_tbeam();
    
    // initialize all elements in abp array to be -1 
    for (int i = 0; i < NUM_CARS; i++) {
        abp_array[i] = -1;
    }
}

void loop() {
    uint8_t pck[DATA_PCK_LEN];
    int16_t st = radio.receive(pck, DATA_PCK_LEN);
    
    // make sure receive is properly receieved
    if (st != RADIOLIB_ERR_NONE) {
        return;
    }

    // get the packet's length
    int pck_len = radio.getPacketLength();
    // if at least one packet arrived get the sender_id and abp
    uint8_t sender_id;
    uint8_t abp;
    if (pck_len >= 2) { 
        sender_id = pck[0];
        abp = pck[1];
    }
    else { abp = 0; }

    // bad length
    if (pck_len != DATA_PCK_LEN) {
        Serial.printf("Bad length=%d -> ACK(BAD_LEN) ABP=%u\n", pck_len, abp);
        send_ack(sender_id, abp, 2);
        return;
    }

    // get the actual 30 byte data
    const uint8_t* data = pck + 2;

    // duplicate
    if (abp_array[sender_id] == (int)abp) {
        Serial.printf("DUPLICATE ABP=%u -> ACK(DUPLICATE)\n", abp);
        send_ack(sender_id, abp, 1);
        return;
    }

    // otherwise it is a new packet so update the abp in the array and send the packet
    abp_array[sender_id] = (int)abp;

    send_ack(sender_id, abp, 0);

    // headers for the type of data
    char *headers[17] = { "Time", "BMS_Disch_Enable",
    "Pack_Voltage", "Pack_Current",
    "Pack_Temp", "State_of_Charge",
    "Min_Cell_Voltage", "BMS_LV_Input",
    "Torque_Feedback", "RPM",
    "Flux_Feedback", "InlineAcc",
    "LateralAcc", "VerticalAcc",
    "RollRate", "PitchRate", "YawRate"};


    // print to serial monitor 
    /* Outputs something similar to the following
        Car_ID=1
        ABP=0
        Time=0x00A3
        BMS_Disch_Enable=0x0001
        Pack_Voltage=0x10B2
    */
    Serial.printf("Car_ID=%u\n", sender_id);
    Serial.printf("ABP=%u\n", abp);

    uint16_t actual_data[17] = {
        pck[3] << 8 | pck[2],
        pck[5] << 8 | pck[4],
        pck[7] << 8 | pck[6],
        pck[9] << 8 | pck[8],
        pck[11] << 8 | pck[10],
        pck[13] << 8 | pck[12],
        pck[15] << 8 | pck[14],
        pck[17] << 8 | pck[16],
        pck[19] << 8 | pck[18],
        pck[21] << 8 | pck[20],
        pck[23] << 8 | pck[22],
        pck[25] << 8 | pck[24],
        pck[27] << 8 | pck[26],
        pck[29] << 8 | pck[28],
        pck[31] << 8 | pck[30],
        pck[33] << 8 | pck[32],
        pck[35] << 8 | pck[34]
    };


    for (int i = 0; i < 17; i++) {
        Serial.printf("%s=0x%04X\n", headers[i], actual_data[i]);
    }
    Serial.printf("\n");
}
