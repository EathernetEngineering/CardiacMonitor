#include "common.h"

int start = 0;
MonitorPacket packet;

uint8_t checksum(uint8_t *ptr, size_t sz) {
  uint8_t chk = 0;
  while (sz-- != 0) {
    chk -= *ptr++;
  }
  return chk;
}

void setup() {
  Serial.begin(115200);
  start = millis();
  strcpy(packet.magic, MONITOR_MAGIC);
}

void loop() {
  while (millis() - start <= 9);

  packet.lead1 = 1;
  digitalRead(13); // simulate read
  packet.lead2 = 2;
  digitalRead(13); // simulate read
  packet.lead3 = 4;
  digitalRead(13); // simulate read
  packet.resp  = 8;
  digitalRead(13); // simulate read
  digitalRead(13); // simulate math
  digitalRead(13); // simulate math
  packet.dt = millis() - start;
  start = millis();
  packet.checksum = checksum((uint8_t*)(&packet), sizeof(packet) - sizeof(uint8_t));
  if (Serial)
    Serial.write((const uint8_t*)(&packet), sizeof(packet));
}

