#include "common.h"

void setup() {
  Serial.begin(115200);
}

void loop() {
  MonitorPacket packet;
  strcpy(packet.magic, MONITOR_MAGIC);
  packet.lead1 = 1;
  packet.lead2 = 2;
  packet.lead3 = 4;
  packet.resp  = 8;
  packet.checksum = 0;
  for (int i = sizeof(packet); i != 0; --i) {
    packet.checksum -= *((char*)(&packet) + i);
  }
  Serial.write((const uint8_t*)(&packet), sizeof(packet));
  delay(250);
}

