#include <Wire.h>
#include <RadioLib.h>
#include <XPowersLib.h>
#include <shared_defs.h>

XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// array index is the sender_id
static int abp_array[NUM_CARS];


// send an acknowledgement
static void send_ack(uint16_t sender_id, uint16_t abp, uint16_t status) {
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
    uint16_t pck[PCK_LEN];
    int16_t st = radio.receive((uint8_t*)pck, PCK_LEN * sizeof(uint16_t));
    
    // make sure receive is properly receieved
    if (st != RADIOLIB_ERR_NONE) {
        return;
    }

    // get the packet's length
    int pck_len = radio.getPacketLength();
    // if at least one packet arrived get the sender_id and abp
    uint16_t sender_id;
    uint16_t abp;
    if (pck_len >= 2 * sizeof(uint16_t)) { 
        sender_id = pck[0];
        abp = pck[1];
    }
    else { abp = 0; }

    // bad length
    if (pck_len != (PCK_LEN * sizeof(uint16_t))) {
        Serial.printf("Bad length=%d -> ACK(BAD_LEN) ABP=%u\n", pck_len, abp);
        send_ack(sender_id, abp, 2);
        return;
    }

    // duplicate
    if (abp_array[sender_id] == (int)abp) {
        Serial.printf("DUPLICATE ABP=%u -> ACK(DUPLICATE)\n", abp);
        send_ack(sender_id, abp, 1);
        return;
    }

    // otherwise it is a new packet so update the abp in the array and send the packet
    abp_array[sender_id] = (int)abp;

    // simulate dropping an ACK
    if (random(1, 11) <= 9) send_ack(sender_id, abp, 0);

    // otherwise uncomment below
    // send_ack(sender_id, abp, 0);

    // headers for the type of data
    char *headers[17] = { "Time", "BMS Disch Enable",
    "Pack Voltage", "Pack Current",
    "Pack Temp", "State of Charge",
    "Min Cell Voltage", "BMS LV input",
    "Torque Feedback", "RPM",
    "Flux Feedback", "InlineAcc",
    "LateralAcc", "VerticalAcc",
    "RollRate", "PitchRate", "YawRate"};

    // print to serial monitor 
    /* Outputs something similar to the following
        Car_ID: 1 | ABP: 0
            Time                : 0x00A3
            BMS Disch Enable    : 0x0001
            Pack Voltage        : 0x10B2
            ..
    */
    Serial.printf("\nCar_ID: %u | ABP: %u\n", sender_id, abp);

    for (int i = 2; i < PCK_LEN; i++) {
        Serial.printf("  %-20s: %04X\n", headers[i-2], pck[i]);
    }
}
