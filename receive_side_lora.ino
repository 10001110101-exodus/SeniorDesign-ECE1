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

XPowersAXP2101 PMU;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

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

String get_msg() {
  if (digitalRead(LORA_DIO1) == HIGH) {
    String str;
    int state = radio.readData(str);
    radio.startReceive(); // reset to listen mode
    if (state == RADIOLIB_ERR_NONE) return str;
  }
  return "";
}

void setup() {
  power_up_tbeam();
}

void loop() {
  String incoming = get_msg();
  
  if (incoming != "") {
    Serial.print("got data: ");
    Serial.println(incoming);
    Serial.print("rssi: ");
    Serial.println(radio.getRSSI());
  }
}