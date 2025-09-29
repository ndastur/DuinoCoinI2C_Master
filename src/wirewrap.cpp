#include "wirewrap.h"
#include <Wire.h>

// Just supporting ESP32 here
#define SDA 21
#define SCL 22

#define WIRE_CLOCK 100000
#define WIRE_MAX 32
#define REPEATED_WIRE_SEND_COUNT 1      // 1 for AVR, 8 for RP2040


void wirewrap_setup() {
  //pinMode(SDA, INPUT_PULLUP);
  //pinMode(SCL, INPUT_PULLUP);

  wire_start();
  wire_read_all();
}

void wire_start()
{
  Wire.begin(SDA, SCL);
  Wire.setClock(WIRE_CLOCK);
}

void wirewrap_loop() {
  // poll sensors / peripherals if needed
}

void wire_read_all()
{
  for (byte address = 1; address < WIRE_MAX; address++ )
  {
    if (wire_exists(address))
    {
      Serial.print("Address: ");
      Serial.println(address);
      wire_readline(address);
    }
  }
}

void wire_send_all(String message)
{
  for (byte address = 1; address < WIRE_MAX; address++ )
  {
    if (wire_exists(address))
    {
      Serial.print("Address: ");
      Serial.println(address);
      wire_sendln(address, message);
    }
  }
}

boolean wire_exists(byte address)
{
  wire_start();
  Wire.beginTransmission(address);
  byte error = Wire.endTransmission();
  return (error == 0);
}

void wire_send_job(byte address, String lastblockhash, String newblockhash, int difficulty)
{
  String job = lastblockhash + "," + newblockhash + "," + difficulty;
  wire_sendln(address, job);
}

void wire_sendln(byte address, String message)
{
  wire_send(address, message + "\n");
}

void wire_send_cmd(byte address, String message)
{
  wire_send(address, message + '$');
}

void wire_send(byte address, String message)
{
  wire_start();
  
  int i=0;
  while (i < message.length())
  {
    if (wire_run_every_micro(500)) {
      Wire.beginTransmission(address);
      for (int j=0; j < REPEATED_WIRE_SEND_COUNT; j++) {
        Wire.write(message.charAt(i));
      }
      Wire.endTransmission();
      i++;
    }
  }
}

String wire_readline(int address)
{
  wire_run_every(0);
  char end = '\n';
  String str = "";
  boolean done = false;
  wire_start();
  while (!done)
  {
    Wire.requestFrom(address, 1);
    if (Wire.available())
    {
      char c = Wire.read();
      //Serial.print(c);
      if (c == end)
      {
        break;
        done = true;
      }
      str += c;
    }
    if (wire_run_every(2000)) break;
  }
  //str += end;
  return str;
}

boolean wire_run_every(unsigned long interval)
{
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    return true;
  }
  return false;
}

boolean wire_run_every_micro(unsigned long interval)
{
  static unsigned long previousMicros = 0;
  unsigned long currentMicros = micros();
  if (currentMicros - previousMicros >= interval)
  {
    previousMicros = currentMicros;
    return true;
  }
  return false;
}
