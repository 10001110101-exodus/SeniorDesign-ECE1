// shared definitions for easy changing between user_end and car_end

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


// constants for data sending
#define DATA_PCK_LEN    36
#define HEADER_LEN      2                               // sender_id & abp
#define DATA_BYTES      (DATA_PCK_LEN - HEADER_LEN)     
#define ACK_LEN         3

#define MAX_RETRIES     5
#define ACK_TIMEOUT_MS  1000


// constants for data receiving
#define NUM_CARS        3

// CAN interface pins
#define CAN_TX_PIN 13
#define CAN_RX_PIN 14
