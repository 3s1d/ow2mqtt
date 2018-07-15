/*
*    emulation of DS2334 https://github.com/orgua/OneWireHub used for LED PWM control
*/

//todo handle millis overflow. we will run indefenetely
//no bootup sequence when set is only a few seconds old...

#include <math.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "OneWireHub.h"
#include "DS2423.h"

template <typename T> T sign(T value)
{
  return T((value>0)-(value<0));
}

constexpr uint8_t pin_onewire   { 8 };

auto hub = OneWireHub(pin_onewire);
auto ds2423 = DS2423(DS2423::family_code, 0x00, 0x00, 0x23, 0x24, 0xDA, 0x00);
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

uint32_t writes;      //counter 0, page 12
const uint32_t WRITE_COUNTER = 0;
const uint32_t WRITE_PAGE = 12;

#define numCh         16
uint16_t duration = 1000;               //time in ms to reach color
uint8_t chtarget[numCh] = {0};          //target
uint8_t chcur[numCh] = {0};             //current 
int16_t nthIt[numCh] = {0};
int16_t passedIt[numCh] = {0};
uint16_t gamma[256];

//fade increment every 5ms hard coded

//todo debug macro. led pwm table. pwm

void eval_ow_cmd(char *cmd)
{
  if(cmd == NULL || strlen(cmd) == 0)
    return;

  char *p = strtok(cmd, ",");
  do
  {
    //Debug
    Serial.println(p);

    //todo startup?
    if(strlen(p) > 2 && p[1] == '=')
    {
      int value = strtol(&p[2], NULL, 16);
      if(p[0] >= '0' && p[0] < '0'+numCh )       //channel setting
      {
          //todo current and desired array...
          int8_t ch = p[0] - '0';
          chtarget[ch] = value;

          int16_t delta = ((int16_t)chtarget[ch]) - ((int16_t)chcur[ch]);
          if(duration < numCh || delta == 0)
            nthIt[ch] = 0;                   //instant
          else
            nthIt[ch] = (int16_t)round( ((float)(duration/5)) / ((float)delta) );
            
          passedIt[ch] = 0;     
      }
      else if(p[0] == 't')                //time setting
      {
          duration = value;
      }
    }
  }
  while((p=strtok(NULL, ",")) != NULL);
}

void setup()
{
  Serial.begin(115200);
  delay(2000);                              //for testing only. init output before
  Serial.println("OneWire-Hub DS2423");

  // Build up gamma pattern
  float gammaf   = 2.8;   // Correction factor
  int   max_in  = 255,    // Top end of INPUT range
        max_out = 4095;   // Top end of OUTPUT range
  for(int x=0; x<256; x++)
  {
    gamma[x] = round(pow((float)x / (float)max_in, gammaf) * max_out);
    //Serial.print(x);
    //Serial.print('>');
    //Serial.println(gamma[x]);
  }

  char mem_write[32];
  int length = snprintf(mem_write, sizeof(mem_write), "0,0,0,0,0");
  ds2423.writeMemory((uint8_t *)mem_write, length+1, 0);

  // Setup OneWire
  hub.attach(ds2423);
  writes = ds2423.getCounter(WRITE_COUNTER);

  // Setup PWM
  pwm.begin();
  pwm.setPWMFreq(200);  // max is 1.6kHz
  //eval_ow_cmd("");
  //eval_ow_cmd("1=34");
  eval_ow_cmd("t=1388,0=F0");     //t can be used to set variable speeds for difference channels as well...
  
}

void loop()
{
  static uint8_t last_channel[numCh] = {0};
  static unsigned long nexttime;
  
  // following function must be called periodically
  hub.poll();

  /* new date available */
  if(writes != ds2423.getCounter(WRITE_COUNTER))
  {
    /* update counter */
    writes = ds2423.getCounter(WRITE_COUNTER);
  
    /* get new data */
    uint8_t mem_read[32];
    const uint16_t pos = WRITE_PAGE * 32;
    ds2423.readMemory(mem_read, sizeof(mem_read), pos);
    eval_ow_cmd((char *)mem_read);
  }

  /* prepare for read */
  bool changed = false;
  for(int i=0; i<numCh && !changed; i++)
    if(chcur[i] != last_channel[i])
      changed = true;
  if(changed)
  {
    Serial.println("Store new values");
    
    for(int i=0; i<numCh; i++)
      last_channel[i] = chcur[i];

    char mem_write[numCh*2 + 1];
    int len = 0;
    for(int i=0; i<numCh; i++)
      len += snprintf(&mem_write[len], sizeof(mem_write)-len, "%02X", last_channel[i]);
    ds2423.writeMemory((uint8_t *)mem_write, len+1, 0);
  }

  /* fade */
  if(millis() > nexttime)
  {
    nexttime = millis() + 4;    //every 5ms

    for(int i=0; i<numCh; i++)
    {
      if(chcur[i] == chtarget[i])
        continue;

      if(nthIt[i] == 0)         //instant
        chcur[i] = chtarget[i];
      else if(++passedIt[i] >= fabs(nthIt[i]))
      {
        chcur[i] += sign(nthIt[i]);
        passedIt[i] = 0;
      }

      /* set value */
      uint16_t val = gamma[chcur[i]];
      if(val == 0)
        pwm.setPWM(i, 0, 4096); //off
      else if(val == 4095)
        pwm.setPWM(i, 4096, 0); //on
     else
       pwm.setPWM(i, 0, val);
/*
      Serial.print("t");
      Serial.print(millis());
      Serial.print(" ch");
      Serial.print(i);
      Serial.print(" cur");
      Serial.print(chcur[i]);
      Serial.print(" tar");
      Serial.print(chtarget[i]);
      Serial.print(" nit");
      Serial.print(nthIt[i]);
      Serial.print(" pit");
      Serial.print(passedIt[i]);
      Serial.print(" val");
      Serial.println(val);
*/
    }
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
