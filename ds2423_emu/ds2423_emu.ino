/*
*    Example-Code that emulates a DS2423 4096 bits RAM with Counter
*
*   THIS DEVICE IS ONLY A MOCKUP FOR NOW - NO REAL FUNCTION
*
*   Tested with
*   - DS9490R-Master, atmega328@16MHz and teensy3.2@96MHz as Slave
*/

#include "OneWireHub.h"
#include "DS2423.h"

constexpr uint8_t pin_onewire   { 8 };

auto hub = OneWireHub(pin_onewire);
auto ds2423 = DS2423(DS2423::family_code, 0x00, 0x00, 0x23, 0x24, 0xDA, 0x00);

uint32_t writes_pwm;      //counter 0, page 12
uint32_t writes_startup;  //counter 1, page 13
const uint32_t WRITE_PWM_COUNTER = 0;
const uint32_t WRITE_STARTUP_COUNTER = 1;
const uint32_t WRITE_PWM_PAGE = 12;
const uint32_t WRITE_STARTUP_PAGE = 12+1;

uint16_t duration = 0;                  //time in ms to reach color
uint8_t channel[5] = {0, 0, 0, 0, 0};   //Red (6bit), Green (6bit), Blue (6bit), Cool White (8bit), Warm White (8bit)

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println("OneWire-Hub DS2423");

    // Setup OneWire
    hub.attach(ds2423);

    writes_pwm =  ds2423.getCounter(WRITE_PWM_COUNTER);
    writes_startup = ds2423.getCounter(WRITE_STARTUP_COUNTER);
}

void loop()
{
    static int last_channel[5] = {-1, -1, -1, -1, -1};
  
    // following function must be called periodically
    hub.poll();

    /* new date available */
    if(writes_pwm != ds2423.getCounter(WRITE_PWM_COUNTER))
    {
        /* update counter */
        writes_pwm = ds2423.getCounter(WRITE_PWM_COUNTER);

        /* get new data */
        uint8_t mem_read[32];
        const uint16_t pos = WRITE_PWM_PAGE * 32;
        ds2423.readMemory(mem_read, sizeof(mem_read), pos);

        Serial.println("Received");
        Serial.print(mem_read[0], HEX);
        Serial.print(",");
        Serial.print(mem_read[1], HEX);
        Serial.print(",");
        Serial.print(mem_read[2], HEX);
        Serial.print(",");
        Serial.print(mem_read[3], HEX);
        Serial.print(",");
        Serial.print(mem_read[4], HEX);
        Serial.print(",");
        Serial.print(mem_read[5], HEX);
        Serial.print(",");
        Serial.println(mem_read[6], HEX);        
    }

    /* prepare for read */
    //todo use registers
    if(channel[0] != last_channel[0] || channel[1] != last_channel[1] || channel[2] != last_channel[2] || channel[3] != last_channel[3] || channel[4] != last_channel[4])
    {
        //Serial.println("New values");
        
        for(int i=0; i<5; i++)
          last_channel[i] = channel[i];

        char mem_write[32];
        int length = snprintf(mem_write, sizeof(mem_write), "%d,%d,%d,%d,%d", last_channel[0], last_channel[1], last_channel[2], last_channel[3], last_channel[4]);
        ds2423.writeMemory(mem_write, length+1, 0);
    }

    //uint8_t t[512];
    //ds2423.readMemory(t, 512, 0);
    //for(int i=0; i<512; i++)
    //{
    //    if(t[i] != 0)
    //      Serial.println(i);
    //    
    //}
    
} 
