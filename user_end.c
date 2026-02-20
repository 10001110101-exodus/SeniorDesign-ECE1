#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>


/*PLACEHOLDER*/


// PIN STUFF HERE
#define LORA_SCK   5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS   18
#define LORA_RST  14
#define LORA_BUSY 23
#define LORA_DIO1 26

// RADIO STUFF HERE
SPIClass SPI_LORA(HSPI);
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, SPI_LORA);





/*PLACEHOLDER*/


// constants for data sending
#define DATA_PCK_LEN 32
#define DATA_BYTES   31
#define ACK_LEN      2


// send an acknowledgement
static void send_ack(uint8_t abp, uint8_t status) {
    // create a two byte acknowledgement and send
    uint8_t ack[ACK_LEN];
    ack[0] = (uint8_t)(abp & 0xFF);
    ack[1] = status;
    radio.transmit(ack, ACK_LEN);
}

void setup() {
    Serial.begin(#NUMBER HERE);
    delay(200);


    SPI_LORA.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

    float freqMHz = #NUMBER HERE; 
    int16_t st = radio.begin(freqMHz);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("LoRa begin failed: %d\n", st);
        while (1) delay(1000);
    }

    Serial.println("Ready on user end");
}

void loop() {
    static int last_delivered_abp = -1;

    uint8_t pck[DATA_PCK_LEN];
    int16_t st = radio.receive(pck, DATA_PCK_LEN); /
    
    // make sure receive is properly receieved
    if (st != RADIOLIB_ERR_NONE) {
        return;
    }

    // get the packet's length
    int pck_len = radio.getPacketLength();
    // if at least one packet arrived get the abp 
    uint8_t abp;
    if (pck_len >= 1) { abp = pck[0]; }
    else { abp = 0; }

    // bad length
    if (pck_len != DATA_PCK_LEN) {
        Serial.printf("Bad length=%d -> ACK(BAD_LEN) ABP=%u\n", pck_len, abp);
        rx_send_ack(abp, 2);
        return;
    }

    // get the actual 31 byte data
    const uint8_t* data = pck + 1;

    // duplicate
    if (last_delivered_abp == (int)abp) {
        Serial.printf("DUPLICATE ABP=%u -> ACK(DUPLICATE)\n", abp);
        rx_send_ack(abp, 1);
        return;
    }

    // new packet
    last_delivered_abp = (int)abp;

    rx_send_ack(abp, 0);
}