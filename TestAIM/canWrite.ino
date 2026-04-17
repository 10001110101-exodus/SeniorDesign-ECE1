/*
  CANWrite

  Write and send CAN Bus messages

  See the full documentation here:
  https://docs.arduino.cc/tutorials/uno-r4-wifi/can
*/

/**************************************************************************************
 * INCLUDE
 **************************************************************************************/

#include <Arduino_CAN.h>
/**************************************************************************************
 * CONSTANTS
 **************************************************************************************/

//static uint32_t const CAN_ID = 0x20;

/**************************************************************************************
 * SETUP/LOOP
 **************************************************************************************/

void setup()
{
  Serial.begin(115200);
  while (!Serial) { }

  if (!CAN.begin(CanBitRate::BR_1000k))
  {
    Serial.println("CAN.begin(...) failed.");
    for (;;) {}
  }
}

static uint32_t msg_cnt = 0;

void loop()
{
  /* Assemble a CAN message with the format of
   * 0xCA 0xFE 0x00 0x00 [4 byte message counter]
   */
    //uint8_t const msg_data[] = {0xCA,0xFE,0,0,0,0,0,0};
    if(Serial.available() > 0)
    {
      // note:
      // being sent such that 
      // byte 0 is lower byte
      // byte 1 is higher byte

        unsigned char buffer[6];
        int n = Serial.readBytes(buffer, 6);
        uint32_t canid = (buffer[1] << 8) | buffer[0];
        char buf[20];

        unsigned char msg_data[8];
        //memcpy(msg_data, buffer + 2, 4 * sizeof(unsigned char));
        msg_data[0] = buffer[2];
        msg_data[1] = buffer[3];
        msg_data[2] = buffer[4];
        msg_data[3] = buffer[5];
        if(canid != 0)
        {
            CanMsg const msg(CanStandardId(canid), sizeof(msg_data), (uint8_t*)&msg_data);
            if (int const rc = CAN.write(msg); rc < 0)
            {
              Serial.println("CAN.begin(...) failed.");
              Serial.println(rc);
                for (;;) { }
            }
            else{
              sprintf(buf, "%d", *msg_data);
              Serial.println(buf);
            }
            
        }
        
        
    }
  

  /* Transmit the CAN message, capture and display an
   * error core in case of failure.
   */
  

  /* Only send one message per second. */

}
