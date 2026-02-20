#include <RadioLib.h>
#include <Wire.h>
#include <XPowersLib.h>

#define I2C_SDA 21
#define I2C_SCL 22
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_DIO1 33
#define LORA_BUSY 32
#define USER_BUTTON 38 // side button between PWR and RST

XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// false if send error
bool send_msg(String text) {
  int state = radio.transmit(text);
  radio.startReceive(); // go back to listen mode
  if (state == RADIOLIB_ERR_NONE) return true;
  else return false;
}

String get_msg() {
  if (digitalRead(LORA_DIO1) == HIGH) {
    String str;
    int state = radio.readData(str);
    radio.startReceive(); 
    if (state == RADIOLIB_ERR_NONE) return str;
  }
  return "";
}

void setup() {
  Serial.begin(115200);
  while(!Serial);

  // setup power chip
  Wire.begin(I2C_SDA, I2C_SCL);
  PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  PMU.setALDO2Voltage(3300); 
  PMU.enableALDO2();
  PMU.setALDO3Voltage(1800); // for the tcxo clock
  PMU.enableALDO3();
  PMU.setDLDO1Voltage(3300); 
  PMU.enableDLDO1();

  // radio init
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  // 915.0 frequency, 125.0 bandwidth, 7/4 CR, 1.8v tcxo
  int state = radio.begin(915.0, 125.0, 7, 7, 0x12, 17, 8, 1.8, true);
  
  if (state == RADIOLIB_ERR_NONE) {
    radio.setTCXO(1.8);
    //
    radio.setDio2AsRfSwitch();
    radio.startReceive();
    Serial.println("Board ready");
  }

  pinMode(USER_BUTTON, INPUT_PULLUP);
}

void loop() {
  // check for incoming
  String incoming = get_msg();
  if (incoming != "") {
    Serial.println("Received: " + incoming);
    Serial.print("RSSI: ");
    Serial.println(radio.getRSSI());
  }

  // send if button pressed
  if (digitalRead(USER_BUTTON) == LOW) {
    if (send_msg("Hello from other board")) {
      Serial.println("Sent OK");
    }
    delay(300);
  }
}