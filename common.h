#ifndef _COMMON_H
#define _COMMON_H

#define MONITOR_MAGIC_LENGTH 5
#define MONITOR_MAGIC ".MON"

struct __attribute__((__packed__)) MonitorPacket {
	char magic[5];
	int lead1;
	int lead2;
	int lead3;
	int resp;
	uint8_t checksum;
};

#endif

