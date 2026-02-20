#include <RadioLib.h>
#include <Wire.h>
#include <XPowersLib.h>

// hardware pins
#define I2C_SDA 21
#define I2C_SCL 22
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_DIO1 33
#define LORA_BUSY 32
#define USER_BUTTON 38

XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);



// constants for data sending
#define DATA_PCK_LEN    32
#define DATA_BYTES      31
#define ACK_LEN         2

#define MAX_RETRIES     5
#define ACK_TIMEOUT_MS  1000

// make a packet given the abp and the actual data
static void make_packet(uint8_t abp, const uint8_t data[DATA_BYTES], uint8_t out_packet[DATA_PCK_LEN]) {
    out_packet[0] = (uint8_t)(abp & 0xFF);
    memcpy(out_packet + 1, data, DATA_BYTES);
}

// wait for ACK (the abp num, status) for the expected abp num
// return 1 if received, 0 if timeout
static int wait_for_ack(uint8_t expected_abp, uint8_t *status_out) {
    uint32_t start = millis();      // start recording the time
    uint8_t ack[ACK_LEN];

    while ( ( millis() - start ) < ACK_TIMEOUT_MS ) {
        int16_t st = radio.receive(ack, ACK_LEN, 100);

        // check that it was received properly
        if (st == RADIOLIB_ERR_NONE) {
            // make sure it's the right abp
            if (ack[0] == (expected_abp & 0xFF) ) {
                // set the status of the ack
                *status_out = ack[1];
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
        uint_8 ack_status = 0xFF;
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
    power_up_tbeam()
    delay(200);
    pinMode(USER_BUTTON, INPUT_PULLUP);
}

static uint32_t counter = 0;

void loop() {
    static uint8_t abp = 0;

    //uint8_t payload[DATA_BYTES];
    uint8_t packet[DATA_PCK_LEN];


    // dummy data for now 
    uint8_t payload[DATA_BYTES] = {0x1B, 0x4C, 0x9A, 0x2F, 0x6E, 0x7C, 0x8D, 0x5A,
        0x0F, 0x3E, 0x2D, 0x1C, 0x5B, 0x4A, 0x99, 0x2E, 0x6D, 0x7B, 0x8C,
        0x49, 0x0E, 0x3D, 0x2C, 0x1B, 0x5A, 0x49, 0x98, 0x2D, 0x8F, 0x1A, 0x5C}

    make_packet(abp, payload, packet);

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
