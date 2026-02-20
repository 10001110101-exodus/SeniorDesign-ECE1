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

bool send_msg(String text) {
  int state = radio.transmit(text);
  return (state == RADIOLIB_ERR_NONE);
}

void setup() {
  power_up_tbeam();
  pinMode(USER_BUTTON, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(USER_BUTTON) == LOW) {
    Serial.println("sending...");
    if (send_msg("team_update_01")) {
      Serial.println("sent successfully");
    }
    delay(500); // debounce
  }
}