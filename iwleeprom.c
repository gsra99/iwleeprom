/*
****************************************************************************
*
* iwleeprom - EEPROM reader/writer for intel wifi cards.
* Copyright (C) 2010, Alexander "ittrium" Kalinichenko <alexander@kalinichenko.org>
* ICQ: 152322, Skype: ittr1um		
* Copyright (C) 2010, Gennady "ShultZ" Kozlov <qpxtool@mail.ru>
*
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
****************************************************************************
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <getopt.h>
#include <endian.h>

//#define DEBUG
#define MMAP_LENGTH 4096
#define EEPROM_SIZE_4965 1024
#define EEPROM_SIZE_5K   2048

#define EEPROM_SIZE_MAX  2048

#define IWL_SIGNATURE  0x5a40

uint16_t buf[EEPROM_SIZE_MAX/2];

static struct option long_options[] = {
	{"device",    1, NULL, 'd'},
	{"nodev",     0, NULL, 'n'},
	{"preserve-mac", 0, NULL, 'm'},
	{"preserve-calib", 0, NULL, 'c'},
	{"read",      0, NULL, 'r'},
	{"write",     0, NULL, 'w'},
	{"ifile",     1, NULL, 'i'},
	{"ofile",     1, NULL, 'o'},
	{"bigendian", 0, NULL, 'b'},
	{"help",      0, NULL, 'h'},
	{"list",      0, NULL, 'l'},
	{"debug",     1, NULL, 'D'},
	{"init",      0, NULL, 'I'},
	{"patch11n",  0, NULL, 'p'},
	{"all-channels",  0, NULL, 'a'},
	{"show-regulatory",  0, NULL, 's'}
};

struct pcidev_id
{
	unsigned int class,
				ven,  dev,
				sven, sdev;
	int 	idx;
	size_t  eeprom_size;
	char *device;
};

struct pci_id
{
	unsigned int	ven, dev;
	bool			writable;
	size_t			eeprom_size;
	char name[64];
};

enum byte_order
{
	order_unknown = 0,
	order_be,
	order_le
};

int mem_fd;
char *mappedAddress;
unsigned int offset;
unsigned char eeprom_locked;
enum byte_order dump_order;
uid_t ruid,euid,suid;

void die(  const char* format, ... ); 
char	*ifname = NULL,
		*ofname = NULL;
bool patch11n = false,
	 all_channels = false,
	 show_regulatory = false,
	 init_device = false;
	 nodev = false,
	 preserve_mac = false,
	 preserve_calib = false;

unsigned int  debug = 0;

struct pcidev_id dev;

#define DEVICES_PATH "/sys/bus/pci/devices"

struct pci_id valid_ids[] = {
	{ 0x8086, 0x0082, 0, EEPROM_SIZE_5K,   "6000 Series Gen2"},
	{ 0x8086, 0x0083, 0, EEPROM_SIZE_5K,   "Centrino Wireless-N 1000"},
	{ 0x8086, 0x0084, 0, EEPROM_SIZE_5K,   "Centrino Wireless-N 1000"},
	{ 0x8086, 0x0085, 0, EEPROM_SIZE_5K,   "6000 Series Gen2"},
	{ 0x8086, 0x0087, 0, EEPROM_SIZE_5K,   "Centrino Advanced-N + WiMAX 6250"},
	{ 0x8086, 0x0089, 0, EEPROM_SIZE_5K,   "Centrino Advanced-N + WiMAX 6250"},
	{ 0x8086, 0x0885, 0, EEPROM_SIZE_5K,   "WiFi+WiMAX 6050 Series Gen2"},
	{ 0x8086, 0x0886, 0, EEPROM_SIZE_5K,   "WiFi+WiMAX 6050 Series Gen2"},
	{ 0x8086, 0x4229, 1, EEPROM_SIZE_4965, "PRO/Wireless 4965 AG or AGN [Kedron] Network Connection"},
	{ 0x8086, 0x422b, 0, EEPROM_SIZE_5K,   "Centrino Ultimate-N 6300"},
	{ 0x8086, 0x422c, 0, EEPROM_SIZE_5K,   "Centrino Advanced-N 6200"},
	{ 0x8086, 0x4230, 1, EEPROM_SIZE_4965, "PRO/Wireless 4965 AG or AGN [Kedron] Network Connection"},
	{ 0x8086, 0x4232, 1, EEPROM_SIZE_5K,   "WiFi Link 5100"},
	{ 0x8086, 0x4235, 1, EEPROM_SIZE_5K,   "Ultimate N WiFi Link 5300"},
	{ 0x8086, 0x4236, 1, EEPROM_SIZE_5K,   "Ultimate N WiFi Link 5300"},
	{ 0x8086, 0x4237, 1, EEPROM_SIZE_5K,   "PRO/Wireless 5100 AGN [Shiloh] Network Connection"},
	{ 0x8086, 0x4238, 0, EEPROM_SIZE_5K,   "Centrino Ultimate-N 6300"},
	{ 0x8086, 0x4239, 0, EEPROM_SIZE_5K,   "Centrino Advanced-N 6200"},
	{ 0x8086, 0x423a, 1, EEPROM_SIZE_5K,   "PRO/Wireless 5350 AGN [Echo Peak] Network Connection"},
	{ 0x8086, 0x423b, 1, EEPROM_SIZE_5K,   "PRO/Wireless 5350 AGN [Echo Peak] Network Connection"},
	{ 0x8086, 0x423c, 1, EEPROM_SIZE_5K,   "WiMAX/WiFi Link 5150"},
	{ 0x8086, 0x423d, 1, EEPROM_SIZE_5K,   "WiMAX/WiFi Link 5150"},

	{ 0, 0, 0, 0, "" }
};

#if BYTE_ORDER == BIG_ENDIAN
#define cpu2le16(x) __bswap_16(x)
#define cpu2be16(x) x
#define le2cpu16(x) __bswap_16(x)
#define be2cpu16(x) x
#elif BYTE_ORDER == LITTLE_ENDIAN
#define cpu2le16(x) x
#define cpu2be16(x) __bswap_16(x)
#define le2cpu16(x) x
#define be2cpu16(x) __bswap_16(x)
#else
#error Unsupported BYTE_ORDER!
#endif

void eeprom_lock()
{
	unsigned long data;
	if (nodev) return;
	memcpy(&data, mappedAddress, 4);
	data |= 0x00200000;
	memcpy(mappedAddress, &data, 4);
	usleep(5);
	memcpy(&data, mappedAddress, 4);
	if ((data & 0x00200000) != 0x00200000)
		die("err! ucode is using eeprom!\n");
	eeprom_locked = 1;
}

void eeprom_unlock()
{
	unsigned long data;
	if (nodev) return;
	memcpy(&data, mappedAddress, 4);
	data &= ~0x00200000;
	memcpy(mappedAddress, &data, 4);
	usleep(5);
	memcpy(&data, mappedAddress, 4);
	if ((data & 0x00200000) == 0x00200000)
		die("err! software is still using eeprom!\n");
	eeprom_locked = 0;
}

void init_card()
{
	if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
		printf("cannot open /dev/mem\n");
		exit(1);
	}

	mappedAddress = (char *)mmap(NULL, MMAP_LENGTH, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, mem_fd, offset);
	if (mappedAddress == MAP_FAILED) {
		perror("mmap failed");
		exit(1);
	}
}

void initpower()
{
	unsigned int data;
	memcpy(&data, mappedAddress + 0x100, 4);
	data |= 0x20000000;
	memcpy(mappedAddress + 0x100, &data, 4);
	usleep(20);
	memcpy(&data, mappedAddress + 0x100, 4);
	data |= 0x00800000;
	memcpy(mappedAddress + 0x100, &data, 4);
	usleep(20);
	memcpy(&data, mappedAddress + 0x240, 4);
	data |= 0xFFFF0000;
	memcpy(mappedAddress + 0x240, &data, 4);
	usleep(20);
	memcpy(&data, mappedAddress + 0x00, 4);
	data |= 0x00080000;
	memcpy(mappedAddress + 0x00, &data, 4);
	usleep(20);
	memcpy(&data, mappedAddress + 0x20c, 4);
	data |= 0x00880300;
	memcpy(mappedAddress + 0x20c, &data, 4);
	usleep(20);
	memcpy(&data, mappedAddress + 0x24, 4);
	data |= 0x00000004;
	memcpy(mappedAddress + 0x24, &data, 4);
	usleep(20);

	if (debug)
		printf("Device has been inited.\n");
}

void release_card()
{
	if (mappedAddress != NULL)
		munmap(mappedAddress, MMAP_LENGTH);
}

void init_dump(char *filename)
{
	FILE *fd;

	seteuid(ruid);
	if (!(fd = fopen(filename, "rb")))
		die("Can't read file %s\n", filename);
	dev.eeprom_size = 2 * fread(buf, 2, EEPROM_SIZE_MAX/2, fd);
	fclose(fd);
	seteuid(suid);

	printf("Dump file: %s (read %Ld bytes)\n", filename, (uint64_t) dev.eeprom_size);
	if(dev.eeprom_size < EEPROM_SIZE_4965)
		die("Too small file!\n");

	if ( IWL_SIGNATURE == le2cpu16(buf[0])) {
		dump_order = order_le;
	} else if ( IWL_SIGNATURE == be2cpu16(buf[0])) {
		dump_order = order_be;
	} else {
		die("Invalid EEPROM signature!\n");
	}
	printf("  byte order: %s ENDIAN\n", (dump_order == order_le) ? "LITTLE":"BIG");
}

void fixate_dump(char *filename)
{
	FILE *fd;
	seteuid(ruid);

	if (!(fd = fopen(filename, "wb")))
		die("Can't create file %s\n", filename);
	fwrite(buf, dev.eeprom_size, 1, fd);
	printf("Dump file written: %s\n", filename);
	fclose(fd);

	seteuid(suid);
}

const uint16_t eeprom_read16(unsigned int addr)
{
	uint16_t value;
	if (nodev) goto _nodev;

	unsigned int data = 0x0000FFFC & (addr << 1);
	memcpy(mappedAddress + 0x2c, &data, 4);
	usleep(50);
	memcpy(&data, mappedAddress + 0x2c, 4);
	if ((data & 1) != 1)
		die("Read not complete! Timeout at %.4dx\n", addr);

	value = (data & 0xFFFF0000) >> 16;
	return value;

_nodev:
	if (addr >= EEPROM_SIZE_MAX) return 0;
	if (dump_order == order_le)
		return (le2cpu16(buf[addr/2]));
	else
		return (be2cpu16(buf[addr/2]));
}

void eeprom_write16(unsigned int addr, uint16_t value)
{
	if (nodev) goto _nodev;
	unsigned int data = value;

	if (preserve_mac && ((addr>=0x2A && addr<0x30) || (addr>=0x92 && addr<0x97)))
		return;
	if (preserve_calib && (addr >= 0x200))
		return;

	data <<= 16;
	data |= 0x0000FFFC & (addr << 1);
	data |= 0x2;

	memcpy(mappedAddress + 0x2c, &data, 4);
	usleep(5000);

	data = 0x0000FFC & (addr << 1);
	memcpy(mappedAddress + 0x2c, &data, 4);
	usleep(50);
	memcpy(&data, mappedAddress + 0x2c, 4);
	if ((data & 1) != 1)
		die("Read not complete! Timeout at %.4dx\n", addr);
	if (value != (data >> 16))
		die("Verification error at %.4x\n", addr);
	return;
_nodev:
	if (addr >= EEPROM_SIZE_MAX) return;
	if (dump_order == order_le)
		buf[addr/2] = cpu2le16(value);
	else
		buf[addr/2] = cpu2be16(value);
}

void eeprom_read(char *filename)
{
	unsigned int addr = 0;
	FILE *fd;
	dev.eeprom_size = valid_ids[dev.idx].eeprom_size;
	
	seteuid(ruid);
	if (!(fd = fopen(filename, "wb")))
		die("Can't create file %s\n", filename);
	seteuid(suid);

	eeprom_lock();

	printf("Saving dump with byte order: %s ENDIAN\n", (dump_order == order_le) ? "LITTLE":"BIG");

	for (addr = 0; addr < dev.eeprom_size; addr += 2)
	{
		if (dump_order == order_le)
			buf[addr/2] = cpu2le16( eeprom_read16(addr) );
		else
			buf[addr/2] = cpu2be16( eeprom_read16(addr) );
		if (0 ==(addr & 0x7F)) printf("%04x [", addr);
		printf(".");
		if (0x7E ==(addr & 0x7F)) printf("]\n");
		fflush(stdout);
	}

	seteuid(ruid);
	fwrite(buf, dev.eeprom_size, 1, fd);
	fclose(fd);
	seteuid(suid);

	eeprom_unlock();

	printf("\nEEPROM has been dumped to %s\n", filename);
}

void eeprom_write(char *filename)
{
	unsigned int addr = 0;
	enum byte_order order = order_unknown;
	uint16_t value;
	size_t   size;
	FILE *fd;
	dev.eeprom_size = valid_ids[dev.idx].eeprom_size;

	seteuid(ruid);
	if (!(fd = fopen(filename, "rb")))
		die("Can't read file %s\n", filename);
	size = 2 * fread(buf, 2, dev.eeprom_size/2, fd);
	fclose(fd);
	seteuid(suid);

	eeprom_lock();	

	for(addr=0; addr<size;addr+=2)
	{
		if (order == order_unknown) {
			if ( IWL_SIGNATURE == le2cpu16(buf[addr/2])) {
				order = order_le;
			} else if ( IWL_SIGNATURE == be2cpu16(buf[addr/2])) {
				order = order_be;
			} else {
				die("Invalid EEPROM signature!\n");
			}
			printf("Dump file byte order: %s ENDIAN\n", (order == order_le) ? "LITTLE":"BIG");
		}
		if (order == order_be)
			value = be2cpu16( buf[addr/2] );
		else
			value = le2cpu16( buf[addr/2] );

		if (0 ==(addr & 0x7F)) printf("%04x [", addr);
		if (eeprom_read16(addr) != value) {
			eeprom_write16(addr, value);
			printf(".");
		} else {
			printf("=");
		}
		if (0x7E ==(addr & 0x7F)) printf("]\n");
		fflush(stdout);
	}

	eeprom_unlock();

	printf("\nEEPROM has been written from %s\n", filename);
}


struct regulatory_item
{
	unsigned int addr;
	uint16_t	 data;
	uint16_t	 chn;
};


#define HT40 0x100
struct regulatory_item regulatory[] =
{
/*
	BAND 2.4GHz (@15e-179 with regulatory base @156)
*/
// enabling channels 12-14 (1-11 should be enabled on all cards)
	{ 0x1E, 0x0f21, 12 },
	{ 0x20, 0x0f21, 13 },
	{ 0x22, 0x0f21, 14 },

/*
	BAND 5GHz
*/
// subband 5170-5320 MHz (@198-1af)
//	{ 0x42, 0x0fe1, 34 },
	{ 0x44, 0x0fe1, 36 },
//	{ 0x46, 0x0fe1, 38 },
	{ 0x48, 0x0fe1, 40 },
//	{ 0x4a, 0x0fe1, 42 },
	{ 0x4c, 0x0fe1, 44 },
//	{ 0x4e, 0x0fe1, 46 },
	{ 0x50, 0x0fe1, 48 },
	{ 0x52, 0x0f31, 52 },
	{ 0x54, 0x0f31, 56 },
	{ 0x56, 0x0f31, 60 },
	{ 0x58, 0x0f31, 64 },

// subband 5500-5700 MHz (@1b2-1c7)
	{ 0x5c, 0x0f31, 100 },
	{ 0x5e, 0x0f31, 104 },
	{ 0x60, 0x0f31, 108 },
	{ 0x62, 0x0f31, 112 },
	{ 0x64, 0x0f31, 116 },
	{ 0x66, 0x0f31, 120 },
	{ 0x68, 0x0f31, 124 },
	{ 0x6a, 0x0f31, 128 },
	{ 0x6c, 0x0f31, 132 },
	{ 0x6e, 0x0f31, 136 },
	{ 0x70, 0x0f31, 140 },

// subband 5725-5825 MHz (@1ca-1d5)
//	{ 0x74, 0x0fa1, 145 },
	{ 0x76, 0x0fa1, 149 },
	{ 0x78, 0x0fa1, 153 },
	{ 0x7a, 0x0fa1, 157 },
	{ 0x7c, 0x0fa1, 161 },
	{ 0x7e, 0x0fa1, 165 },

/*
	BAND 2.4GHz, HT40 channels (@1d8-1e5)
*/
	{ 0x82, 0x0e6f, HT40 + 1 },
	{ 0x84, 0x0f6f, HT40 + 2 },
	{ 0x86, 0x0f6f, HT40 + 3 },
	{ 0x88, 0x0f6f, HT40 + 4 },
	{ 0x8a, 0x0f6f, HT40 + 5 },
	{ 0x8c, 0x0f6f, HT40 + 6 },
	{ 0x8e, 0x0f6f, HT40 + 7 },

/*
	BAND 5GHz, HT40 channels (@1e8-1fd)
*/
	{ 0x92, 0x0fe1, HT40 +  36 },
	{ 0x94, 0x0fe1, HT40 +  44 },
	{ 0x96, 0x0f31, HT40 +  52 },
	{ 0x98, 0x0f31, HT40 +  60 },
	{ 0x9a, 0x0f31, HT40 + 100 },
	{ 0x9c, 0x0f31, HT40 + 108 },
	{ 0x9e, 0x0f31, HT40 + 116 },
	{ 0xa0, 0x0f31, HT40 + 124 },
	{ 0xa2, 0x0f31, HT40 + 132 },
	{ 0xa4, 0x0f61, HT40 + 149 },
	{ 0xa6, 0x0f61, HT40 + 157 },

	{ 0, 0}
};

struct regulatory_item regulatory_all[] =
{
/*
	BAND 1
*/
	{ 0x08, 0x0f6f, 1 },
	{ 0x0A, 0x0f6f, 2 },
	{ 0x0C, 0x0f6f, 3 },
	{ 0x0E, 0x0f6f, 4 },
	{ 0x10, 0x0f6f, 5 },
	{ 0x12, 0x0f6f, 6 },
	{ 0x14, 0x0f6f, 7 },
	{ 0x16, 0x0f6f, 8 },
	{ 0x18, 0x0f6f, 9 },
	{ 0x1A, 0x0f6f, 10 },
	{ 0x1C, 0x0f6f, 11 },
	{ 0x1E, 0x0f61, 12 },
	{ 0x20, 0x0f61, 13 },
	{ 0x22, 0x0f61, 14 }, //not orig

/*
	BAND 2 (all values not orig)
*/
/*
	{ 0x26, 0x0, 183 },
	{ 0x28, 0x0, 184 },
	{ 0x2A, 0x0, 185 },
	{ 0x2C, 0x0, 187 },
	{ 0x2E, 0x0, 188 },
	{ 0x30, 0x0, 189 },
	{ 0x32, 0x0, 192 },
	{ 0x34, 0x0, 196 },
	{ 0x36, 0x0, 7 },
	{ 0x38, 0x0, 8 },
	{ 0x3A, 0x0, 11 },
	{ 0x3C, 0x0, 12 },
	{ 0x3E, 0x0, 16 },
*/

/*
	BAND 3
*/
	{ 0x42, 0x0fe1, 34 }, //not orig
	{ 0x44, 0x0fe1, 36 },
	{ 0x46, 0x0fe1, 38 }, //not orig
	{ 0x48, 0x0fe1, 40 },
	{ 0x4A, 0x0fe1, 42 }, //not orig
	{ 0x4C, 0x0fe1, 44 },
	{ 0x4E, 0x0fe1, 46 }, //not orig
	{ 0x50, 0x0fe1, 48 },
	{ 0x52, 0x0f31, 52 },
	{ 0x54, 0x0f31, 56 },
	{ 0x56, 0x0f31, 60 },
	{ 0x58, 0x0f31, 64 },

/*
	BAND 4
*/
	{ 0x5C, 0x0f31, 100 },
	{ 0x5E, 0x0f31, 104 },
	{ 0x60, 0x0f31, 108 },
	{ 0x62, 0x0f31, 112 },
	{ 0x64, 0x0f31, 116 },
	{ 0x66, 0x0f31, 120 },
	{ 0x68, 0x0f31, 124 },
	{ 0x6A, 0x0f31, 128 },
	{ 0x6C, 0x0f31, 132 },
	{ 0x6E, 0x0f31, 136 },
	{ 0x70, 0x0f31, 140 },

/*
	BAND 5
*/
	{ 0x74, 0x0fe1, 145 }, //not orig
	{ 0x76, 0x0fe1, 149 },
	{ 0x78, 0x0fe1, 153 },
	{ 0x7A, 0x0fe1, 157 },
	{ 0x7C, 0x0fe1, 161 },
	{ 0x7E, 0x0fe1, 165 },

/*
	BAND 6
*/
	{ 0x82, 0x0f6f, HT40 + 1 },
	{ 0x84, 0x0f6f, HT40 + 2 },
	{ 0x86, 0x0f6f, HT40 + 3 },
	{ 0x88, 0x0f6f, HT40 + 4 },
	{ 0x8A, 0x0f6f, HT40 + 5 },
	{ 0x8C, 0x0f6f, HT40 + 6 },
	{ 0x8E, 0x0f6f, HT40 + 7 },

/*
	BAND 7
*/
	{ 0x92, 0x0fe1, HT40 + 36 },
	{ 0x94, 0x0fe1, HT40 + 44 },
	{ 0x96, 0x0f31, HT40 + 52 },
	{ 0x98, 0x0f31, HT40 + 60 },
	{ 0x9A, 0x0f31, HT40 + 100 },
	{ 0x9C, 0x0f31, HT40 + 108 },
	{ 0x9E, 0x0f31, HT40 + 116 },
	{ 0xA0, 0x0f31, HT40 + 124 },
	{ 0xA2, 0x0f31, HT40 + 132 },
	{ 0xA4, 0x0f61, HT40 + 149 },
	{ 0xA6, 0x0f61, HT40 + 157 },

	{ 0, 0}
};

void eeprom_show_regulatory()
{
	uint16_t value;
	unsigned int reg_offs;
	int idx;

	printf("Regulatory data from card EEPROM...\n");

	eeprom_lock();	

	reg_offs = 2 * eeprom_read16(0xCC);
	printf("Regulatory base: %04x\n", reg_offs);

	for (idx=0; regulatory_all[idx].addr; idx++) {
		value = eeprom_read16(reg_offs + regulatory_all[idx].addr);
		printf("Channel %d%s:\t%04x\n", regulatory_all[idx].chn & ~HT40, (regulatory_all[idx].chn & HT40) ? " (HT40)" : "", value);
	}

	eeprom_unlock();
}

void eeprom_all_channels()
{
	uint16_t value;
	unsigned int reg_offs;
	int idx;

	printf("Write regulatory data for all channels unlock...\n");

	eeprom_lock();	

	printf("-> Checking and adding channels...\n");

	reg_offs = 2 * eeprom_read16(0xCC);
	printf("Regulatory base: %04x\n", reg_offs);

	for (idx=0; regulatory_all[idx].addr; idx++) {
		value = eeprom_read16(reg_offs + regulatory_all[idx].addr);
		if (value != regulatory_all[idx].data) {
			printf("  %d%s\n", regulatory_all[idx].chn & ~HT40, (regulatory_all[idx].chn & HT40) ? " (HT40)" : "");
			eeprom_write16(reg_offs + regulatory_all[idx].addr, regulatory_all[idx].data);
		}
	}

	eeprom_unlock();

	printf("\nCard EEPROM patched successfully\n");
}

void eeprom_patch11n()
{
	uint16_t value;
	unsigned int reg_offs;
	int idx;

	printf("Patching card EEPROM...\n");

	eeprom_lock();	

	printf("-> Changing subdev ID\n");
	value = eeprom_read16(0x14);
	if ((value & 0x000F) == 0x0006) {
		eeprom_write16(0x14, (value & 0xFFF0) | 0x0001);
	}
/*
enabling .11n

W @8A << 00F0 (00B0) <- xxxx xxxx x1xx xxxx
W @8C << 103E (603F) <- x001 xxxx xxxx xxx0
*/

	printf("-> Enabling 11n mode\n");
// SKU_CAP
	value = eeprom_read16(0x8A);
	if ((value & 0x0040) != 0x0040) {
		printf("  SKU CAP\n");
		eeprom_write16(0x8A, value | 0x0040);
	}

// OEM_MODE
	value = eeprom_read16(0x8C);
	if ((value & 0x7001) != 0x1000) {
		printf("  OEM MODE\n");
		eeprom_write16(0x8C, (value & 0x9FFE) | 0x1000);
	}

/*
writing SKU ID - 'MoW' signature
*/
	if (eeprom_read16(0x158) != 0x6f4d) eeprom_write16(0x158, 0x6f4d);
	if (eeprom_read16(0x15A) != 0x0057) eeprom_write16(0x15A, 0x0057);

	printf("-> Checking and adding channels...\n");
// reading regulatory offset
	reg_offs = 2 * eeprom_read16(0xCC);
	printf("Regulatory base: %04x\n", reg_offs);
/*
writing channels regulatory...
*/
	for (idx=0; regulatory[idx].addr; idx++) {
		if (eeprom_read16(reg_offs + regulatory[idx].addr) != regulatory[idx].data) {
			printf("  %d%s\n", regulatory[idx].chn & ~HT40, (regulatory[idx].chn & HT40) ? " (HT40)" : "");
			eeprom_write16(reg_offs + regulatory[idx].addr, regulatory[idx].data);
		}
	}

	eeprom_unlock();
	printf("\nCard EEPROM patched successfully\n");
}

void die(  const char* format, ... ) {
	va_list args;
	fprintf(stderr, "\n\E[31;60m");
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
	fprintf(stderr, "\E[0m");

	release_card();
	exit(1);
}

unsigned int read_id(const char *device, const char* param)
{
	FILE *f;
	unsigned int id;
	char path[512];
	sprintf(path, DEVICES_PATH "/%s/%s", device, param);
	if (!(f = fopen(path, "r")))
		return 0;
	fscanf(f,"%x", &id);
	fclose(f);
	return id;
}

void check_device(struct pcidev_id *id)
{
	int i;

	id->idx = -1;
	id->class = (read_id(id->device,"class") >> 8);
	if (!id->class) {
		printf("No such PCI device: %s\n", id->device);
	}
	id->ven   = read_id(id->device,"vendor");
	id->dev   = read_id(id->device,"device");
	id->sven  = read_id(id->device,"subsystem_vendor");
	id->sdev  = read_id(id->device,"subsystem_device");

	for(i=0; id->idx<0 && valid_ids[i].ven; i++)
		if(id->ven==valid_ids[i].ven && id->dev==valid_ids[i].dev)
			id->idx = i;
}

void list_supported()
{
	int i;
	printf("Known devices:\n");
	for(i=0; valid_ids[i].ven; i++)
		printf("  %04x:%04x [%s] %s \n", 
			valid_ids[i].ven,
			valid_ids[i].dev,
			valid_ids[i].writable ? "RW" : "RO",
			valid_ids[i].name);
}

void map_device()
{
	FILE *f;
	char path[512];
	unsigned char data[64];
	int i;
	unsigned int addr;
	sprintf(path, DEVICES_PATH "/%s/%s", dev.device, "config");
	if (!(f = fopen(path, "r")))
		return;
	fread(data, 64, 1, f);
	fclose(f);

	for (i=0x10; !offset && i<0x28;i+=4) {
		addr = ((unsigned int*)data)[i/4];
		if ((addr & 0xF) == 4)
			offset = addr & 0xFFFFFFF0;
	}
}

void search_card()
{
	DIR  *dir;
	struct dirent *dentry;
	struct pcidev_id id;
	struct pcidev_id *ids = NULL;
	int i,cnt=0;

	dir = opendir(DEVICES_PATH);
	if (!dir)
		die("Can't list PCI devices\n");
	if (debug)
		printf("PCI devices:\n");
	id.device = (char*) malloc(256 * sizeof(char));
	do {
		dentry = readdir(dir);
		if (!dentry || !strncmp(dentry->d_name, ".", 1))
			continue;

		strcpy(id.device, dentry->d_name);
		check_device(&id);
		if (debug) {
			printf("    %s: class %04x   id %04x:%04x   subid %04x:%04x",
				id.device,
				id.class,
				id.ven,  id.dev,
				id.sven, id.sdev
			);
			if (id.idx < 0)
				printf("\n");
			else
				printf(" [%s] %s \n", 
					valid_ids[id.idx].writable ? "RW" : "RO",
					valid_ids[id.idx].name);
		}
		if (id.idx >=0 ) {
			if(!cnt)
				ids = (struct pcidev_id*) malloc(sizeof(id));
			else
				ids = (struct pcidev_id*) realloc(ids, (cnt+1)*sizeof(id));
			ids[cnt].device = (char*) malloc(256 * sizeof(char));
			ids[cnt].class = id.class;
			ids[cnt].ven = id.ven; ids[cnt].sven = id.sven;
			ids[cnt].dev = id.dev; ids[cnt].sdev = id.sdev;
			ids[cnt].idx = id.idx;
			memcpy(ids[cnt].device, id.device, 256);
			cnt++;
		}
	}
	while (dentry);
	printf("Supported devices detected: %s\n", cnt ? "" : "\n  NONE");
	if (!cnt) goto nodev;

	for (i=0; i<cnt; i++) {
		printf("  [%d] %s [%s] %s (%04x:%04x, %04x:%04x)\n", i+1,
			ids[i].device,
			valid_ids[ids[i].idx].writable ? "RW" : "RO",
			valid_ids[ids[i].idx].name,
			ids[i].ven,  ids[i].dev,
			ids[i].sven, ids[i].sdev);
	}
	i++;
	while(i<=0 || i>cnt) {
		if (!i)	goto out;
		printf("Select device [1-%d] (or 0 to quit): ", cnt);
		scanf("%d", &i);
	}
	i--;
	dev.device = (char*) malloc(256 * sizeof(char));
	memcpy(dev.device, ids[i].device, 256);

out:
	free(id.device);
	for (i=0; i<cnt; i++) free(ids[i].device);
	free(ids);
	return;
nodev:
	free(id.device);
	exit(1);
}

int main(int argc, char** argv)
{
	char c;
	dev.device = NULL;
	dump_order = order_le;
	getresuid(&ruid, &euid, &suid);

	while (1) {
		c = getopt_long(argc, argv, "rwld:mcni:o:bhpasID:", long_options, NULL);
		if (c == -1)
			break;
		switch(c) {
			case 'l':
				list_supported();
				exit(0);
			case 'd':
				dev.device = optarg;
				break;
			case 'n':
				nodev = true;
				break;
			case 'm':
				preserve_mac = true;
				break;
			case 'c':
				preserve_calib = true;
				break;
			case 'r':
				die("option -r deprecated. use -o instead\n");
				break;
			case 'o':
				ofname = optarg;
				break;
			case 'w':
				die("option -w deprecated. use -i instead\n");
				break;
			case 'i':
				ifname = optarg;
				break;
			case 'b':
				dump_order = order_be;
				break;
			case 'p':
				patch11n = true;
				break;
			case 'a':
				all_channels = true;
				break;
			case 's':
				show_regulatory = true;
				break;
			case 'I':
				init_device = true;
				break;
			case 'D':
				debug = atoi(optarg);
				if (debug)
					printf("debug level: %s\n", optarg);
				break;
			case 'h':
				die("EEPROM reader/writer for intel wifi cards\n\n"
					"Usage:\n"
					"\t%s [-d device] [-r filename [-b]] [-w filename] [-p] [-D debug_level]\n"
					"\t%s -l\n\n"
					"Options:\n"
					"\t-d <device> | --device <device>\t\t"
					"device in format 0000:00:00.0 (domain:bus:dev.func)\n"
					"\t-n | --nodev\t"
					"don't touch any device, file-only operations\n"
					"\t-m | --preserve-mac\t"
					"don't change card's MAC while writing full eeprom dump\n"
					"\t-c | --preserve-calib\t"
					"don't change calibration data while writing full eeprom dump\n"
					"\t-o <filename> | --ofile <filename>\t"
					"dump eeprom to binary file\n"
					"\t-i <filename> | --ifile <filename>\t"
					"write eeprom from binary file\n"
					"\t-b | --bigendian\t\t\t"
					"save dump in big-endian byteorder (default: little-endian)\n"
					"\t-p | --patch11n\t\t\t\t"
					"patch device eeprom to enable 802.11n\n"
					"\t-a | --all-channels\t\t\t\t"
					"patch device eeprom to enable all channels\n"
					"\t-s | --show-regulatory\t\t\t\t"
					"show regulatory eeprom data\n"
					"\t-I | --init\t\t\t\t"
					"init device (useful if driver didn't it)\n"
					"\t-l | --list\t\t\t\t"
					"list known cards\n"
					"\t-D <level> | --debug <level>\t\t"
					"set debug level (0-1, default 0)\n"
					"\t-h | --help\t\t\t\t"
					"show this info\n", argv[0], argv[0]);
			default:
				return 1;
		}
	}

	if (nodev) goto _nodev;

	if (!dev.device) search_card();
	if (!dev.device) exit(1);
	check_device(&dev);

	if (!dev.class)	exit(2);
	if (dev.idx < 0)
		die("Selected device not supported\n");

	printf("Using device %s [%s] %s \n",
		dev.device,
		valid_ids[dev.idx].writable ? "RW" : "RO",
		valid_ids[dev.idx].name);

	map_device();

	if (!offset)
		die("Can't obtain memory address\n");

	if (debug)
		printf("address: %08x\n", offset);

	if(!ifname && !ofname && !patch11n)
		printf("No file names given or patch option selected!\nNo EEPROM actions will be performed, just write-enable test\n");

	if(init_device)
		initpower();

	init_card();

	if (show_regulatory)
		eeprom_show_regulatory();

	if (ofname)
		eeprom_read(ofname);

	if (ifname && valid_ids[dev.idx].writable)
		eeprom_write(ifname);

	if (patch11n && valid_ids[dev.idx].writable)
		eeprom_patch11n();

	if (all_channels)
		eeprom_all_channels();

	release_card();
	return 0;

_nodev:
	if (dev.device)
		die("Don't use '-d' and '-n' options simultaneously\n");

	printf("Device-less operation...\n");
	if (!ifname || !ofname)
		die("Have to specify both input and output files!\n");
	init_dump(ifname);
	if (patch11n)
		eeprom_patch11n();

	fixate_dump(ofname);
	return 0;
}

