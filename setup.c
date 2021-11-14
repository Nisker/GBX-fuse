/*
 GBxCart RW - Console Interface
 Version: 1.35
 Author: Alex from insideGadgets (www.insidegadgets.com)
 Modified by: Nisker
 Created: 7/11/2016
 Last Modified: 14/11/2021
 License: CC-BY-NC-SA
 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <time.h>
#else
#define _XOPEN_SOURCE 600
#include <time.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
#define RS232_PORTNR  57
#else
#define RS232_PORTNR  30
#endif

#include <stdio.h>
#include "setup.h"

// COM Port settings (default)
#include "rs232/rs232.h"
int cport_nr = 7; // /dev/ttyS7 (COM8 on windows)
int bdrate = 1000000; // 1,000,000 baud

// Common vars
uint8_t gbxcartFirmwareVersion = 0;
uint8_t gbxcartPcbVersion = 0;
uint8_t readBuffer[257];
uint8_t writeBuffer[257];
char optionSelected = 0;

char gameTitle[17];
uint16_t cartridgeType = 0;
uint32_t currAddr = 0x0000;
uint32_t endAddr = 0x7FFF;
uint16_t romSize = 0;
uint32_t romEndAddr = 0;
uint16_t romBanks = 0;
int ramSize = 0;
uint16_t ramBanks = 0;
uint32_t ramEndAddress = 0;
int eepromSize = 0;
uint16_t eepromEndAddress = 0;
int hasFlashSave = 0;
uint8_t cartridgeMode = GB_MODE;
uint32_t bytesReadPrevious = 0;
uint32_t ledStatus = 0;
uint32_t ledCountLeft = 0;
uint32_t ledCountRight = 0;
uint8_t ledSegment = 0;
uint8_t ledProgress = 0;
uint8_t ledBlinking = 0;
uint8_t headerCheckSumOk = 0;
uint8_t fastReadEnabled = 0;
uint32_t lastAddrHash = 0;
uint8_t idBuffer[2];

static const uint8_t nintendoLogoGBA[] = {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
										0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21, 0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20,
										0x10, 0x46, 0x4A, 0x4A, 0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF, 
										0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1, 0x94, 0x56, 0x8A, 0xC0, 0x13, 0x72, 0xA7, 0xFC, 
										0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61, 0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76, 
										0x23, 0x1D, 0xC7, 0x61, 0x03, 0x04, 0xAE, 0x56, 0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD, 
										0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1, 0x97, 0xFB, 0xC0, 0x85, 0x60, 0xD6, 0x80, 0x25, 
										0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2, 0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44, 
										0x78, 0x00, 0x90, 0xCB, 0x88, 0x11, 0x3A, 0x94, 0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF, 
										0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72, 0x21, 0xD4, 0xF8, 0x07};


// Read the config.ini file for the COM port to use and baud rate
void read_config(void) {
	FILE* configfile = fopen ("config.ini" , "rt");
	if (configfile != NULL) {
		if (fscanf(configfile, "%d\n%d", &cport_nr, &bdrate) != 2) {
			fprintf(stderr, "Config file is corrupt\n");
		}
		else {
			cport_nr--;
		}
		fclose(configfile);
	}
	else {
		fprintf(stderr, "Config file not found\n");
	}
}

// Write the config.ini file for the COM port to use and baud rate
void write_config(void) {
	FILE *configfile = fopen("config.ini", "wt");
	if (configfile != NULL) {
		fprintf(configfile, "%d\n%d\n", cport_nr+1, bdrate);
		fclose(configfile);
	}
}

// Load a file which contains the cartridge RAM settings (only needed if Erase RAM option was used, only applies to GBA games)
void load_cart_ram_info(void) {
	char titleFilename[30];
	strncpy(titleFilename, gameTitle, 20);
	strncat(titleFilename, ".si", 4);

	// Create a new file
	FILE *infoFile = fopen(titleFilename, "rt");
	if (infoFile != NULL) {
		if (fscanf(infoFile, "%d,%d,%d,", &ramSize, &eepromSize, &hasFlashSave) != 3) {
			fprintf(stderr, "Cart RAM info %s is corrupt\n", titleFilename);
		}
		fclose(infoFile);
	}
}

// Write a file which contains the cartridge RAM settings before it's wiped using Erase RAM (Only applies to GBA games)
void write_cart_ram_info(void) {
	char titleFilename[30];
	strncpy(titleFilename, gameTitle, 20);
	strncat(titleFilename, ".si", 4);
	
	// Check if file exists, if not, write the ram info
	FILE *infoFileRead = fopen(titleFilename, "rt");
	if (infoFileRead == NULL) {
		
		// Create a new file
		FILE *infoFile = fopen(titleFilename, "wt");
		if (infoFile != NULL) {
			fprintf(infoFile, "%d,%d,%d,", ramSize, eepromSize, hasFlashSave);
			fclose(infoFile);
		}
	}
	else {
		fclose(infoFileRead);
	}
}

void delay_ms(uint16_t ms) {
	#if defined (_WIN32)
		Sleep(ms);
	#else
		struct timespec ts;
		ts.tv_sec = ms / 1000;
		ts.tv_nsec = (ms * 1000000) % 1000000000;
		nanosleep(&ts, NULL);
	#endif
}

// Read one letter from stdin
char read_one_letter (void) {
	char c = getchar();
	while (getchar() != '\n' && getchar() != EOF);
	return c;
}

// Print progress
void print_progress_percent (uint32_t bytesRead, uint32_t hashNumber) {
	if (optionSelected == '1' && cartridgeMode == GBA_MODE && fastReadEnabled == 1) {
		if (currAddr >= lastAddrHash) {
			printf("#");
			fflush(stdout);
			lastAddrHash = currAddr + (endAddr / 64); 
		}
	}
	else {
		if ((bytesRead % hashNumber == 0) && bytesRead != 0) {
			if (hashNumber == 64) {
				printf("########");
				fflush(stdout);
			}
			else {
				printf("#");
				fflush(stdout);
			}
		}
	}
}

// Print progress
void led_progress_percent (uint32_t bytesRead, uint32_t divideNumber) {
	if (gbxcartPcbVersion == GBXMAS) {
		if (bytesRead >= bytesReadPrevious) {
			bytesReadPrevious += divideNumber;
			
			if (ledSegment == 0) {
				ledStatus |= (1<<ledCountLeft);
				ledSegment = 1;
				ledCountLeft++;
			}
			else {
				ledSegment = 0;
				ledStatus |= (1<<(ledCountRight+14));
				ledCountRight++;
			}
			xmas_set_leds(ledStatus);
			
			if (ledBlinking <= 14) {
				ledBlinking = ledBlinking + 14;
			}
			else {
				ledBlinking = ledBlinking - 13;
			}
			
			if (ledProgress < 27) {
				xmas_blink_led(ledBlinking);
			}
			ledProgress++;
		}
	}
}

void xmas_set_leds (uint32_t value) {
	if (optionSelected == 3) { // When writing, we need to break out of any commands the PC may have sent
		set_mode('0');
		delay_ms(5);
	}
	set_number(XMAS_VALUE, XMAS_LEDS);
	delay_ms(5);
	set_number(value, 'L');
	delay_ms(5);
}

void xmas_blink_led (uint8_t value) {
	if (optionSelected == 3) { // When writing, we need to break out of any commands the PC may have sent
		set_mode('0');
		delay_ms(5);
	}
	set_number(XMAS_VALUE, XMAS_LEDS);
	delay_ms(5);
	set_mode('B');
	set_mode(value);
	delay_ms(5);
}

void xmas_reset_values (void) {
	ledStatus = 0;
	ledCountLeft = 0;
	ledCountRight = 0;
	ledSegment = 0;
	ledProgress = 0;
	ledBlinking = 0;
	bytesReadPrevious = 0;
}

void xmas_wake_up (void) {
	if (gbxcartPcbVersion == GBXMAS) {
		set_mode('!');
		delay_ms(50);  // Wait for ATmega169 to WDT reset if in idle mode
	}
}

void xmas_setup (uint32_t progressNumber) {
	if (gbxcartPcbVersion == GBXMAS) {
		xmas_wake_up();
		xmas_reset_values();
		xmas_set_leds(0);
		ledBlinking = 1;
		xmas_blink_led(ledBlinking);
		bytesReadPrevious = progressNumber;
	}
}

void gbx_set_done_led (void) {
	if (gbxcartPcbVersion == PCB_1_4) {
		set_mode(DONE_LED_ON);
	}
}

void gbx_set_error_led (void) {
	if (gbxcartPcbVersion == PCB_1_4) {
		set_mode(ERROR_LED_ON);
	}
}

void gbx_cart_power_up (void) {
	if (gbxcartPcbVersion == PCB_1_4) { // If cart isn't powered up then power it on
		uint8_t cartPowered = request_value(QUERY_CART_PWR);
		if (cartPowered == 0) {
			set_mode(CART_PWR_ON);
			delay_ms(500);
			
			// Flush buffer
			uint8_t buffer[65];
			RS232_PollComport(cport_nr, buffer, 64);
		}
	}
}

void gbx_cart_power_down (void) {
	if (gbxcartPcbVersion == PCB_1_4) { // If cart isn't powered up then power it on
		set_mode(CART_PWR_OFF);
		delay_ms(500);
	}
}

// Wait for a "1" acknowledgement from the ATmega
void com_wait_for_ack (void) {
	uint8_t buffer[2];
	uint8_t rxBytes = 0;
	
	while (rxBytes < 1) {
		rxBytes = RS232_PollComport(cport_nr, buffer, 1);
		
		if (rxBytes > 0) {
			if (buffer[0] == '1') {
				break;
			}
			rxBytes = 0;
		}
	}
}

// Stop reading blocks of data
void com_read_stop() {
	RS232_cputs(cport_nr, "0"); // Stop read
	RS232_drain(cport_nr);
	if (gbxcartPcbVersion == GBXMAS) { // Small delay as GBXMAS intercepts these commands
		delay_ms(1);
	}
}

// Continue reading the next block of data
void com_read_cont() {
	RS232_cputs(cport_nr, "1"); // Continue read
	RS232_drain(cport_nr);
	if (gbxcartPcbVersion == GBXMAS) { // Small delay as GBXMAS intercepts these commands
		delay_ms(1);
	}
}

// Test opening the COM port, if can't be open, try autodetecting device on other COM ports
uint8_t com_test_port(void) {
	bdrate = 1000000; // Default
	
	// Check if COM port responds correctly
	if (RS232_OpenComport(cport_nr, bdrate, "8N1") == 0) { // Port opened
		set_mode('0');
		RS232_flushRX(cport_nr);
		
		uint8_t cartridgeMode = request_value(CART_MODE);
		
		// Responded ok
		if (cartridgeMode == GB_MODE || cartridgeMode == GBA_MODE) {
			return 1;
		}
		else {
			RS232_CloseComport(cport_nr);
			
			bdrate = 1700000; // Try 1.7M
			if (RS232_OpenComport(cport_nr, bdrate, "8N1") == 0) { // Port opened
				set_mode('0');
				RS232_flushRX(cport_nr);
				uint8_t cartridgeMode = request_value(CART_MODE);
				
				// Responded ok
				if (cartridgeMode == GB_MODE || cartridgeMode == GBA_MODE) {
					return 1;
				}
			}
			
			bdrate = 1000000;
		}
	}
	
	// If port didn't get opened or responded wrong
	for (uint8_t x = 0; x <= RS232_PORTNR; x++) {
		//printf("Trying port %i\n", x);
		
		if (RS232_OpenComport(x, bdrate, "8N1") == 0) { // Port opened
			cport_nr = x;
			//printf("Port opened, setting mode\n");
			
			// See if device responds correctly
			set_mode('0');
			
			//printf("Requesting value\n");
			uint8_t cartridgeMode = request_value(CART_MODE);
			//printf("Response received\n");
			
			// Responded ok, save the new port number
			if (cartridgeMode == GB_MODE || cartridgeMode == GBA_MODE) {
				//printf("Responded ok\n");
				write_config();
				return 1;
			}
			else {
				//printf("Didn't response ok\n");
				RS232_CloseComport(x);
			}
		}
	}
	
	return 0;
}

// Read 1 to 256 bytes from the COM port and write it to the global read buffer or to a file if specified. 
// When polling the com port it return less than the bytes we want, keep polling and wait until we have all bytes requested. 
// We expect no more than 256 bytes.
uint16_t com_read_bytes (FILE *file, int count) {
	uint8_t buffer[257];
	uint16_t rxBytes = 0;
	uint16_t readBytes = 0;
	
	#if defined(__APPLE__)
	uint8_t timeout = 0;
	#else
	uint16_t timeout = 0;
	#endif
	
	while (readBytes < count) {
		rxBytes = RS232_PollComport(cport_nr, buffer, 64);
		
		if (rxBytes > 0) {
			buffer[rxBytes] = 0;
			
			if (file == NULL) {
				memcpy(&readBuffer[readBytes], buffer, rxBytes);
			}
			else {
				fwrite(buffer, 1, rxBytes, file);
			}
			
			readBytes += rxBytes;
		}
		#if defined(__APPLE__)
		else {
			delay_ms(5);
			timeout++;
			if (timeout >= 50) {
				return readBytes;
			}
		}
		#else
		else {
			timeout++;
			if (timeout >= 20000) {
				return readBytes;
			}
		}
		#endif
	}
	return readBytes;
}

// Read 1-256 bytes from the file (or buffer) and write it the COM port with the command given
void com_write_bytes_from_file(uint8_t command, FILE *file, int count) {
	uint8_t buffer[257];
	buffer[0] = command;

	if (file == NULL) {
		memcpy(&buffer[1], writeBuffer, count);
	}
	else {
		fread(&buffer[1], 1, count, file);
	}
	
	RS232_SendBuf(cport_nr, buffer, (count + 1)); // command + 1-128 bytes
	RS232_drain(cport_nr);
}

// Check if OS can support fast COM port reading
void fast_reading_check(void) {
	set_mode(FAST_READ_CHECK);
   
	uint16_t timeOutCounter = 0;
	uint16_t readCounter = 0;
	fastReadEnabled = 1;
	uint8_t buffer[257];
	
	while (readCounter != 32768) {
		uint8_t rxBytes = RS232_PollComport(cport_nr, buffer, 64);
		if (rxBytes > 0) {
			readCounter += rxBytes;
		}
		timeOutCounter++;
		delay_ms(1);
		
		if (timeOutCounter >= 750) { // Taking too long, exit
			fastReadEnabled = 0;
			break;
		}
	}
}

// Send a single command byte
void set_mode (char command) {
	char modeString[5];
	sprintf(modeString, "%c", command);
	
	RS232_cputs(cport_nr, modeString);
	RS232_drain(cport_nr);
	
	delay_ms(1);
	
	#if defined(__APPLE__)
	delay_ms(5);
	#endif
}

// Send a command with a hex number and a null terminator byte
void set_number (uint32_t number, uint8_t command) {
	char numberString[20];
	sprintf(numberString, "%c%x", command, number);
	
	RS232_cputs(cport_nr, numberString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	delay_ms(1);
	
	//printf("%s\n", numberString);
	
	#if defined(__APPLE__) || defined(__linux__)
	delay_ms(5);
	#endif
}

// Read the cartridge mode
uint8_t read_cartridge_mode (void) {
	set_mode(CART_MODE);
	
	uint8_t buffer[2];
	uint8_t rxBytes = 0;
	while (rxBytes < 1) {
		rxBytes = RS232_PollComport(cport_nr, buffer, 1);
		
		if (rxBytes > 0) {
			return buffer[0];
		}
	}
	
	return 0;
}

// Send 1 byte and read 1 byte
uint8_t request_value (uint8_t command) {
	set_mode(command);
	
	uint8_t buffer[2];
	uint8_t rxBytes = 0;
	uint8_t timeoutCounter = 0;
	
	while (rxBytes < 1) {
		rxBytes = RS232_PollComport(cport_nr, buffer, 1);
		
		if (rxBytes > 0) {
			return buffer[0];
		}
		
		delay_ms(10);
		timeoutCounter++;
		//printf(".");
		if (timeoutCounter >= 25) { // After 250ms, timeout
			return 0;
		}
	}
	
	return 0;
}



// ****** Gameboy / Gameboy Colour functions ******

// Set bank for ROM/RAM switching, send address first and then bank number
void set_bank (uint16_t address, uint8_t bank) {
	char AddrString[15];
	sprintf(AddrString, "%c%x", SET_BANK, address);
	RS232_cputs(cport_nr, AddrString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	delay_ms(5);
	
	char bankString[15];
	sprintf(bankString, "%c%d", SET_BANK, bank);
	RS232_cputs(cport_nr, bankString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	delay_ms(5);
}

// MBC2 Fix (unknown why this fixes reading the ram, maybe has to read ROM before RAM?)
// Read 64 bytes of ROM, (really only 1 byte is required)
void mbc2_fix (void) {
	set_number(0x0000, SET_START_ADDRESS);
	set_mode(READ_ROM_RAM);
	
	uint16_t rxBytes = 0;
	uint8_t byteCount = 0;
	uint8_t tempBuffer[64];
	while (byteCount < 64) {
		rxBytes = RS232_PollComport(cport_nr, tempBuffer, 64);
		
		if (rxBytes > 0) {
			byteCount += rxBytes;
		}
	}
	com_read_stop();
}

// Read the first 384 bytes of ROM and process the Gameboy header information
void read_gb_header (void) {
	currAddr = 0x0000;
	endAddr = 0x0180;
	
	set_number(currAddr, SET_START_ADDRESS);
	set_mode(READ_ROM_RAM);
	
	uint8_t startRomBuffer[385];
	while (currAddr < endAddr) {
		uint8_t comReadBytes = com_read_bytes(READ_BUFFER, 64);
		
		if (comReadBytes == 64) {
			memcpy(&startRomBuffer[currAddr], readBuffer, 64);
			currAddr += 64;
			
			// Request 64 bytes more
			if (currAddr < endAddr) {
				com_read_cont();
			}
		}
		else { // Didn't receive 64 bytes, usually this only happens for Apple MACs
			com_read_stop();
			delay_ms(500);
			
			// Flush buffer
			RS232_PollComport(cport_nr, readBuffer, 64);											
			
			// Start off where we left off
			set_number(currAddr, SET_START_ADDRESS);
			set_mode(READ_ROM_RAM);				
		}
	}
	com_read_stop();
	
	// Blank out game title
	for (uint8_t b = 0; b < 16; b++) {
		gameTitle[b] = 0;
	}
	// Read cartridge title and check for non-printable text
	for (uint16_t titleAddress = 0x0134; titleAddress <= 0x143; titleAddress++) {
		char headerChar = startRomBuffer[titleAddress];
		if ((headerChar >= 0x30 && headerChar <= 0x39) || // 0-9
			 (headerChar >= 0x41 && headerChar <= 0x5A) || // A-Z
			 (headerChar >= 0x61 && headerChar <= 0x7A) || // a-z
			 (headerChar >= 0x24 && headerChar <= 0x29) || // #$%&'()
			 (headerChar == 0x2A) || // *
			 (headerChar == 0x2D) || // -
			 (headerChar == 0x2E) || // .
			 (headerChar == 0x5F) || // _
			 (headerChar == 0x20)) { // Space
			gameTitle[(titleAddress-0x0134)] = headerChar;
		}
		// Replace with an underscore
		else if (headerChar == 0x3A) { //  : 
			gameTitle[(titleAddress - 0x0134)] = '_';
		}
		else {
			gameTitle[(titleAddress-0x0134)] = '\0';
			break;
		}
	}
	printf ("Game title: %s\n", gameTitle);
	
	cartridgeType = startRomBuffer[0x0147];
	romSize = startRomBuffer[0x0148];
	ramSize = startRomBuffer[0x0149];
	
	// ROM banks
	romBanks = 2; // Default 32K
	if (romSize >= 1) { // Calculate rom size
		romBanks = 2 << romSize;
	}
	
	// RAM banks
	ramBanks = 0; // Default 0K RAM
	if (cartridgeType == 6) { ramBanks = 1; }
	if (ramSize == 2) { ramBanks = 1; }
	if (ramSize == 3) { ramBanks = 4; }
	if (ramSize == 4) { ramBanks = 16; }
	if (ramSize == 5) { ramBanks = 8; }
	
	// RAM end address
	if (cartridgeType == 6) { ramEndAddress = 0xA1FF; } // MBC2 512bytes (nibbles)
	if (ramSize == 1) { ramEndAddress = 0xA7FF; } // 2K RAM
	if (ramSize > 1) { ramEndAddress = 0xBFFF; } // 8K RAM
	
	printf ("MBC type: ");
	switch (cartridgeType) {
		case 0: printf ("ROM ONLY\n"); break;
		case 1: printf ("MBC1\n"); break;
		case 2: printf ("MBC1+RAM\n"); break;
		case 3: printf ("MBC1+RAM+BATTERY\n"); break;
		case 5: printf ("MBC2\n"); break;
		case 6: printf ("MBC2+BATTERY\n"); break;
		case 8: printf ("ROM+RAM\n"); break;
		case 9: printf ("ROM ONLY\n"); break;
		case 11: printf ("MMM01\n"); break;
		case 12: printf ("MMM01+RAM\n"); break;
		case 13: printf ("MMM01+RAM+BATTERY\n"); break;
		case 15: printf ("MBC3+TIMER+BATTERY\n"); break;
		case 16: printf ("MBC3+TIMER+RAM+BATTERY\n"); break;
		case 17: printf ("MBC3\n"); break;
		case 18: printf ("MBC3+RAM\n"); break;
		case 19: printf ("MBC3+RAM+BATTERY\n"); break;
		case 21: printf ("MBC4\n"); break;
		case 22: printf ("MBC4+RAM\n"); break;
		case 23: printf ("MBC4+RAM+BATTERY\n"); break;
		case 25: printf ("MBC5\n"); break;
		case 26: printf ("MBC5+RAM\n"); break;
		case 27: printf ("MBC5+RAM+BATTERY\n"); break;
		case 28: printf ("MBC5+RUMBLE\n"); break;
		case 29: printf ("MBC5+RUMBLE+RAM\n"); break;
		case 30: printf ("MBC5+RUMBLE+RAM+BATTERY\n"); break;
		case 252: printf("Gameboy Camera\n"); break;
		default: printf ("Not found\n");
	}
	
	printf ("ROM size: ");
	switch (romSize) {
		case 0: printf ("32KByte (no ROM banking)\n"); break;
		case 1: printf ("64KByte (4 banks)\n"); break;
		case 2: printf ("128KByte (8 banks)\n"); break;
		case 3: printf ("256KByte (16 banks)\n"); break;
		case 4: printf ("512KByte (32 banks)\n"); break;
		case 5: 
			if (cartridgeType == 1 || cartridgeType == 2 || cartridgeType == 3) {
				printf ("1MByte (63 banks)\n");
			}
			else {
				printf ("1MByte (64 banks)\n");
			}
			break;
		case 6: 
			if (cartridgeType == 1 || cartridgeType == 2 || cartridgeType == 3) {
				printf ("2MByte (125 banks)\n");
			}
			else {
				printf ("2MByte (128 banks)\n");
			}
			break;
		case 7: printf ("4MByte (256 banks)\n"); break;
		case 8: printf ("8MByte (512 banks)\n"); break;
		case 82: printf ("1.1MByte (72 banks)\n"); break;
		case 83: printf ("1.2MByte (80 banks)\n"); break;
		case 84: printf ("1.5MByte (96 banks)\n"); break;
		default: printf ("Not found\n");
	}
	
	printf ("RAM size: ");
	switch (ramSize) {
		case 0: 
			if (cartridgeType == 6) {
				printf ("512 bytes (nibbles)\n");
			}
			else {
				printf ("None\n");
			}
			break;
		case 1: printf ("2 KBytes\n"); break;
		case 2: printf ("8 KBytes\n"); break;
		case 3: printf ("32 KBytes (4 banks of 8Kbytes)\n"); break;
		case 4: printf ("128 KBytes (16 banks of 8Kbytes)\n"); break;
		case 5: printf ("64 KBytes (8 banks of 8Kbytes)\n"); break;
		default: printf ("Not found\n");
	}
	
	// Header checksum check
	uint8_t romCheckSum = 0;
	for (uint16_t x = 0x0134; x <= 0x014C; x++) {
		romCheckSum = romCheckSum - startRomBuffer[x] - 1;
	}
	printf ("Header Checksum: ");
	if (romCheckSum == startRomBuffer[0x14D]) {
		printf ("OK\n");
		headerCheckSumOk = 1;
	}
	else {
		printf ("Failed\n");
		headerCheckSumOk = 0;
	}
}


// ****** Gameboy Advance functions ****** 

// Check the rom size by reading 64 bytes from different addresses and checking if they are all 0x00. There can be some ROMs 
// that do have valid 0x00 data, so we check 32 different addresses in a 4MB chunk, if 30 or more are all 0x00 then we've reached the end.
uint8_t gba_check_rom_size (void) {
	uint32_t fourMbBoundary = 0x3FFFC0;
	uint32_t currAddr = 0x1FFC0;
	uint8_t romZeroTotal = 0;
	uint8_t romSize = 0;
	
	// Loop until 32MB
	for (uint16_t x = 0; x < 512; x++) {
		set_number(currAddr / 2, SET_START_ADDRESS); // Divide current address by 2 as we only increment it by 1 after 2 bytes have been read on the ATmega side
		set_mode(GBA_READ_ROM);
		
		com_read_bytes(READ_BUFFER, 64);
		com_read_stop();
		
		// Check how many 0x00 are found in the 64 bytes
		uint8_t zeroCheck = 0;
		for (uint16_t c = 0; c < 64; c++) {
			if (readBuffer[c] == 0) {
				zeroCheck++;
			}
		}
		if (zeroCheck >= 64) { // All 0x00's found, set 1 more to the ROM's zero total count
			romZeroTotal++;
		}
		
		// After a 4MB chunk, we check the zeroTotal, if more than 30 then we have reached the end, otherwise reset romZeroTotal
		if (currAddr % fourMbBoundary == 0 || currAddr % fourMbBoundary < 512) {
			if (romZeroTotal >= 30) {
				break;
			}
			
			romZeroTotal = 0;
			romSize += 4;
		}
		
		currAddr += 0x20000; // Increment address by 131K
		
		if (x % 10 == 0) {
			printf(".");
			fflush(stdout);
		}
	}
	
	return romSize;
}

// Used before we write to RAM as we need to check if we have an SRAM or Flash. 
// Write 1 byte to 0x00 on the SRAM/Flash save, if we read it back successfully then we know SRAM is present, then we write
// the original byte back to how it was. This can be a destructive process to the first byte, if anything goes wrong the user
// could lose the first byte, so we only do this check when writing a save back to the SRAM/Flash.
uint8_t gba_test_sram_flash_write (void) {
	printf("Testing for SRAM or Flash presence... ");
	
	// Save the 1 byte first to buffer
	uint8_t saveBuffer[65];
	set_number(0x0000, SET_START_ADDRESS);
	set_mode(GBA_READ_SRAM);
	com_read_bytes(READ_BUFFER, 64);
	memcpy(&saveBuffer, readBuffer, 64);
	com_read_stop();
	
	// Check to see if the first byte matches our test byte (1 in 255 chance), if so, use the another test byte
	uint8_t testNumber = 0x91;
	if (saveBuffer[0] == testNumber) {
		testNumber = 0xA6;
	}
	
	// Write 1 byte
	set_number(0x0000, SET_START_ADDRESS);
	uint8_t tempBuffer[3];
	tempBuffer[0] = GBA_WRITE_ONE_BYTE_SRAM; // Set write sram 1 byte mode
	tempBuffer[1] = testNumber;
	RS232_SendBuf(cport_nr, tempBuffer, 2);
	RS232_drain(cport_nr);
	com_wait_for_ack();
	
	// Read back the 1 byte
	uint8_t readBackBuffer[65];
	set_number(0x0000, SET_START_ADDRESS);
	set_mode(GBA_READ_SRAM);
	com_read_bytes(READ_BUFFER, 64);
	memcpy(&readBackBuffer, readBuffer, 64);
	com_read_stop();
	
	// Verify
	if (readBackBuffer[0] == testNumber) {
		printf("SRAM found\n");
		
		// Write the byte back to how it was
		set_number(0x0000, SET_START_ADDRESS);
		tempBuffer[0] = GBA_WRITE_ONE_BYTE_SRAM; // Set write sram 1 byte mode
		tempBuffer[1] = saveBuffer[0];
		RS232_SendBuf(cport_nr, tempBuffer, 2);
		RS232_drain(cport_nr);
		com_wait_for_ack();
		
		return NO_FLASH_SRAM_FOUND;
	}
	else { // Flash likely present, test by reading the flash ID
		set_mode(GBA_FLASH_READ_ID); // Read Flash ID and exit Flash ID mode
		delay_ms(100);
		
		// Copy to ID buffer
		com_read_bytes(READ_BUFFER, 2);
		memcpy(&idBuffer, readBuffer, 2);
		
		// Some particular flash memories don't seem to exit the ID mode properly, check if that's the case by reading the first byte 
		// from 0x00h to see if it matches any Flash IDs. If so, exit the ID mode a different way and slowly.
		
		// Read from 0x00
		set_number(0x00, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		com_read_bytes(READ_BUFFER, 64);
		memcpy(&readBackBuffer, readBuffer, 64);
		com_read_stop();
		
		// Exit the ID mode a different way and slowly
		if (gbxcartFirmwareVersion <= 25) {
			if (readBackBuffer[0] == 0x1F || readBackBuffer[0] == 0xBF || readBackBuffer[0] == 0xC2 ||
				 readBackBuffer[0] == 0x32 || readBackBuffer[0] == 0x62) {
				
				RS232_cputs(cport_nr, "G"); // Set Gameboy mode
				RS232_drain(cport_nr);
				delay_ms(5);
				
				RS232_cputs(cport_nr, "M0"); // Disable CS/RD/WR/CS2-RST from going high after each command
				RS232_drain(cport_nr);
				delay_ms(5);
				
				RS232_cputs(cport_nr, "OC0xFF"); // Set output lines
				RS232_SendByte(cport_nr, 0);
				RS232_drain(cport_nr);
				delay_ms(5);
				
				RS232_cputs(cport_nr, "HC0xF0"); // Set byte
				RS232_SendByte(cport_nr, 0);
				RS232_drain(cport_nr);
				delay_ms(5);
				
				// V1.1 PCB
				if (gbxcartPcbVersion == PCB_1_1 || gbxcartPcbVersion == PCB_1_3) {
					RS232_cputs(cport_nr, "LD0x40"); // WE low
					RS232_SendByte(cport_nr, 0);
					RS232_drain(cport_nr);
					delay_ms(5);
					
					RS232_cputs(cport_nr, "LE0x04"); // CS2 low
					RS232_SendByte(cport_nr, 0);
					RS232_drain(cport_nr);
					delay_ms(5);
					
					RS232_cputs(cport_nr, "HD0x40"); // WE high
					RS232_SendByte(cport_nr, 0);
					RS232_drain(cport_nr);
					delay_ms(5);
					
					RS232_cputs(cport_nr, "HE0x04"); // CS2 high
					RS232_SendByte(cport_nr, 0);
					RS232_drain(cport_nr);
					delay_ms(5);
				}
				else { // V1.0 PCB
					RS232_cputs(cport_nr, "LD0x90"); // WR, CS2 low
					RS232_SendByte(cport_nr, 0);
					RS232_drain(cport_nr);
					delay_ms(5);
					
					RS232_cputs(cport_nr, "HD0x90"); // WR, CS2 high
					RS232_SendByte(cport_nr, 0);
					RS232_drain(cport_nr);
					delay_ms(5);
				}
				
				delay_ms(50);
				RS232_cputs(cport_nr, "M1"); // Enable CS/RD/WR/CS2-RST goes high after each command
				RS232_drain(cport_nr);
			}
		}
		
		// Invalid Flash ID
		if (idBuffer[0] == 0xFF && idBuffer[1] == 0xFF) {
			return NO_FLASH;
		}
		
		printf("Flash found");
		printf(" (0x%X, 0x%X)\n", idBuffer[0], idBuffer[1]);
		
		
		// Check if it's Atmel Flash
		if (idBuffer[0] == 0x1F) {
			return FLASH_FOUND_ATMEL;
		}
		// Check other manufacturers 
		else if (idBuffer[0] == 0xBF || idBuffer[0] == 0xC2 || 
					idBuffer[0] == 0x32 || idBuffer[0] == 0x62) {
			return FLASH_FOUND;
		}
		
		return NO_FLASH;
	}
}

// Check if SRAM/Flash is present and test the size. 
// When a 256Kbit SRAM is read past 256Kbit, the address is loops around, there are some times where the bytes don't all 
// match up 100%, it's like 90% so be a bit lenient. A cartridge that doesn't have an SRAM/Flash reads all 0x00's.
uint8_t gba_check_sram_flash (void) {
	uint16_t currAddr = 0x0000;
	uint16_t zeroTotal = 0;
	hasFlashSave = NOT_CHECKED;
	
	// Special check for certain games
	if (strncmp(gameTitle, "CHUCHU ROCKE", 12) == 0 || strncmp(gameTitle, "CHUCHUROCKET", 12) == 0) { // Chu-Chu Rocket!
		return SRAM_FLASH_512KBIT;
	}
	
	// Pre-read SRAM/Flash (if the cart has an EEPROM, sometimes D0-D7 come back with random data in the first 64 bytes read)
	set_number(currAddr, SET_START_ADDRESS);
	set_mode(GBA_READ_SRAM);
	com_read_bytes(READ_BUFFER, 64);
	com_read_stop();
	
	//printf("Test if SRAM is present\n");
	
	// Test if SRAM is present, read 32 sections of RAM (64 bytes each)
	for (uint8_t x = 0; x < 32; x++) {
		set_number(currAddr, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		
		com_read_bytes(READ_BUFFER, 64);
		com_read_stop();
		
		// Check for 0x00 byte
		for (uint8_t c = 0; c < 64; c++) {
			if (readBuffer[c] == 0 || readBuffer[c] == 0xFF) {
				zeroTotal++;
			}
		}
		
		currAddr += 0x400;
		
		// Progress
		if (x % 10 == 0) {
			printf(".");
			fflush(stdout);
		}
	}
	
	if (zeroTotal >= 2000) { // Looks like no SRAM or Flash present, lets do a more thorough check
		// Set start and end address
		currAddr = 0x0000;
		endAddr = 32768;
		set_number(currAddr, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		zeroTotal = 0;
		
		// Read data
		while (currAddr < endAddr) {
			com_read_bytes(READ_BUFFER, 64);
			currAddr += 64;
			
			// Check for 0x00 byte
			for (uint8_t c = 0; c < 64; c++) {
				if (readBuffer[c] == 0 || readBuffer[c] == 0xFF) {
					zeroTotal++;
				}
			}
			
			// Request 64 bytes more
			if (currAddr < endAddr) {
				com_read_cont();
			}
		}
		com_read_stop();
		
		//printf("zero total %i\n", zeroTotal);
		
		/*if (zeroTotal == 32768) {
			return 0;
		}*/
	}
	
	uint16_t duplicateCount = 0;
	char firstBuffer[65];
	char secondBuffer[65];
	
	//printf("Lets try writing a byte as the SRAM or Flash is empty\n");
	
	// Lets try writing a byte as the SRAM or Flash is empty
	if (zeroTotal == 32768) {
		// Save the 1 byte first to buffer
		uint8_t saveBuffer[65];
		set_number(0x0000, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		com_read_bytes(READ_BUFFER, 64);
		memcpy(&saveBuffer, readBuffer, 64);
		com_read_stop();
		
		uint8_t testNumber = 0x91;
		
		// Write 1 byte
		set_number(0x0000, SET_START_ADDRESS);
		uint8_t tempBuffer[3];
		tempBuffer[0] = GBA_WRITE_ONE_BYTE_SRAM; // Set write sram 1 byte mode
		tempBuffer[1] = testNumber;
		RS232_SendBuf(cport_nr, tempBuffer, 2);
		RS232_drain(cport_nr);
		com_wait_for_ack();
		
		// Read back the 1 byte
		uint8_t readBackBuffer[65];
		set_number(0x0000, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		com_read_bytes(READ_BUFFER, 64);
		memcpy(&readBackBuffer, readBuffer, 64);
		com_read_stop();
		
		// Verify
		if (readBackBuffer[0] == testNumber) {
			//printf("Wrote ok\n");
			
			// Write the byte back to how it was
			set_number(0x0000, SET_START_ADDRESS);
			tempBuffer[0] = GBA_WRITE_ONE_BYTE_SRAM; // Set write sram 1 byte mode
			tempBuffer[1] = saveBuffer[0];
			RS232_SendBuf(cport_nr, tempBuffer, 2);
			RS232_drain(cport_nr);
			com_wait_for_ack();
			
			return SRAM_FLASH_256KBIT;
		}
	}
	else {
		//printf("Calculate size by checking different addresses (Test 256Kbit or 512Kbit)\n");
		
		// Calculate size by checking different addresses (Test 256Kbit or 512Kbit)
		for (uint8_t x = 0; x < 32; x++) {
			set_number((uint32_t) (x * 0x400), SET_START_ADDRESS);
			set_mode(GBA_READ_SRAM);
			com_read_bytes(READ_BUFFER, 64);
			memcpy(&firstBuffer, readBuffer, 64);
			com_read_stop();
			
			set_number((uint32_t) (x * 0x400) + 0x8000, SET_START_ADDRESS);
			set_mode(GBA_READ_SRAM);
			com_read_bytes(READ_BUFFER, 64);
			memcpy(&secondBuffer, readBuffer, 64);
			com_read_stop();
			
			// Compare
			for (uint8_t x = 0; x < 64; x++) {
				if (firstBuffer[x] == secondBuffer[x]) {
					duplicateCount++;
				}
			}
			
			// Progress
			if (x % 10 == 0) {
				printf(".");
				fflush(stdout);
			}
		}
		
		if (duplicateCount >= 2000) {
			return SRAM_FLASH_256KBIT;
		}
	}
	
	// Check if it's SRAM or Flash at this stage, maximum for SRAM is 512Kbit (or 1Mbit for special flash carts but requires a special command to be sent)
	printf("\n");
	hasFlashSave = gba_test_sram_flash_write();
	if (hasFlashSave == NO_FLASH_SRAM_FOUND) { // Test for 1Mbit special flash cart
		// Switch bank to 0
		gba_flash_write_address_byte(0x1000000, 0x00);
		
		// Save the 1 byte first to buffer
		set_number(0x0000, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		com_read_bytes(READ_BUFFER, 64);
		memcpy(&firstBuffer, readBuffer, 64);
		com_read_stop();
		
		// Switch bank to 1
		gba_flash_write_address_byte(0x1000000, 0x01);
		
		// Save the 1 byte first to buffer
		set_number(0x0000, SET_START_ADDRESS);
		set_mode(GBA_READ_SRAM);
		com_read_bytes(READ_BUFFER, 64);
		memcpy(&secondBuffer, readBuffer, 64);
		com_read_stop();
		
		
		if (firstBuffer[0] != secondBuffer[0]) {
			return SRAM_FLASH_1MBIT;
		}
		else { // Do a more through check of RAM contents
			printf("Testing for 512Kbit or 1Mbit SRAM... ");
			
			duplicateCount = 0;
			for (uint8_t x = 0; x < 32; x++) {
				// Switch bank to 0
				gba_flash_write_address_byte(0x1000000, 0x00);
				
				// Read bank 0
				set_number((uint32_t) (x * 0x400), SET_START_ADDRESS);
				set_mode(GBA_READ_SRAM);
				com_read_bytes(READ_BUFFER, 64);
				memcpy(&firstBuffer, readBuffer, 64);
				com_read_stop();
				
				// Switch bank to 1
				gba_flash_write_address_byte(0x1000000, 0x01);
				
				set_number((uint32_t) (x * 0x400), SET_START_ADDRESS);
				set_mode(GBA_READ_SRAM);
				com_read_bytes(READ_BUFFER, 64);
				memcpy(&secondBuffer, readBuffer, 64);
				com_read_stop();
				
				// Compare
				for (uint8_t x = 0; x < 64; x++) {
					if (firstBuffer[x] == secondBuffer[x]) {
						duplicateCount++;
					}
				}
			}
			
			// Set back to bank to 0
			gba_flash_write_address_byte(0x1000000, 0x00);
			
			// If bank 0 and 1 are duplicated, then it's 512Kbit SRAM
			if (duplicateCount >= 2000) {
				printf("512Kbit\n");
				return SRAM_FLASH_512KBIT;
			}
			else {
				printf("1Mbit\n");
				return SRAM_FLASH_1MBIT;
			}
		}
	}
	
	else if (hasFlashSave == FLASH_FOUND_ATMEL) {
		return SRAM_FLASH_512KBIT;
	}
	
	// Test 512Kbit or 1Mbit Flash, read first 64 bytes on bank 0 then bank 1 and compare
	else if (hasFlashSave == FLASH_FOUND || hasFlashSave == FLASH_FOUND_INTEL) {
		printf("Testing for 512Kbit or 1Mbit Flash... ");
		
		if ((idBuffer[0] == 0xC2 && idBuffer[1] == 0x09) || // Macronix MX29L010
			 (idBuffer[0] == 0x62 && idBuffer[1] == 0x13)) { // SANYO LE26FV10N1TS
			printf("1Mbit\n");
			
			return SRAM_FLASH_1MBIT;
		}
		if ((idBuffer[0] == 0xBF && idBuffer[1] == 0xD4) || // SST 39VF512
			 (idBuffer[0] == 0xC2 && idBuffer[1] == 0x1C) || // Macronix MX29L512
			 (idBuffer[0] == 0x32 && idBuffer[1] == 0x1B)) { // Panasonic MN63F805MNP
			
			printf("512Kbit\n");
			
			return SRAM_FLASH_512KBIT;
		}
		
		duplicateCount = 0;
		for (uint8_t x = 0; x < 32; x++) {
			// Read bank 0
			set_number((uint32_t) (x * 0x400), SET_START_ADDRESS);
			set_mode(GBA_READ_SRAM);
			com_read_bytes(READ_BUFFER, 64);
			memcpy(&firstBuffer, readBuffer, 64);
			com_read_stop();
			
			// Read bank 1
			set_number(1, GBA_FLASH_SET_BANK); // Set bank 1
			
			set_number((uint32_t) (x * 0x400), SET_START_ADDRESS);
			set_mode(GBA_READ_SRAM);
			com_read_bytes(READ_BUFFER, 64);
			memcpy(&secondBuffer, readBuffer, 64);
			com_read_stop();
			
			set_number(0, GBA_FLASH_SET_BANK); // Set back to bank 0
			
			// Compare
			for (uint8_t x = 0; x < 64; x++) {
				if (firstBuffer[x] == secondBuffer[x]) {
					duplicateCount++;
				}
			}
		}
		
		// If bank 0 and 1 are duplicated, then it's 512Kbit Flash
		if (duplicateCount >= 2000) {
			printf("512Kbit\n");
			return SRAM_FLASH_512KBIT;
		}
		else {
			printf("1Mbit\n");
			return SRAM_FLASH_1MBIT;
		}
	}
	
	return 0;
}

// Erase 4K sector on flash on sector address
void flash_4k_sector_erase (uint8_t sector) {
	set_number(sector, GBA_FLASH_4K_SECTOR_ERASE);
}

// Check if an EEPROM is present and test the size. A 4Kbit EEPROM when accessed like a 64Kbit EEPROM sends the first 8 bytes over
// and over again. A cartridge that doesn't have an EEPROM reads all 0x00 or 0xFF.
uint8_t gba_check_eeprom (void) {
	set_number(EEPROM_64KBIT, GBA_SET_EEPROM_SIZE); // Set 64Kbit size
	
	// Set start and end address
	uint16_t currAddr = 0x000;
	uint16_t endAddr = 0x200;
	set_number(currAddr, SET_START_ADDRESS);
	set_mode(GBA_READ_EEPROM);
	
	// Read EEPROM
	uint16_t repeatedCount = 0;
	uint16_t zeroTotal = 0;
	uint8_t firstEightCheck[8];
	while (currAddr < endAddr) {
		com_read_bytes(READ_BUFFER, 8);
		
		if (currAddr == 0) { // Copy the first 8 bytes to check other readings against them
			memcpy(&firstEightCheck, readBuffer, 8);
		}
		else { // Check the 8 bytes for repeats
			for (uint8_t x = 0; x < 8; x++) {
				if (firstEightCheck[x] == readBuffer[x]) {
					repeatedCount++;
				}
			}
		}
		
		// Check for 0x00 or 0xFF bytes
		for (uint8_t x = 0; x < 8; x++) { 
			if (readBuffer[x] == 0 || readBuffer[x] == 0xFF) {
				zeroTotal++;
			}
		}
		
		currAddr += 8;
		
		// Request 8 bytes more
		if (currAddr < endAddr) {
			com_read_cont();
		}
		
		if (currAddr % 20 == 0) {
			printf(".");
			fflush(stdout);
		}
	}
	com_read_stop();
	
	if (zeroTotal >= 512) { // Blank, likely no EEPROM
		return EEPROM_NONE;
	}
	if (repeatedCount >= 400) { // Likely a 4K EEPROM is present
		ramSize = gba_check_sram_flash();
		if (ramSize >= 1) {
			return EEPROM_NONE;
		}
		else {
			return EEPROM_4KBIT;
		}
	}
	else {
		// Additional check for EEPROMs which seem to allow 4Kbit or 64Kbit reads without any issues
		// Check to see if 4Kbit data is repeated in 64Kbit EEPROM mode, if so, it's a 4Kbit EEPROM
		
		// Read first 512 bytes
		currAddr = 0x000;
		endAddr = 0x200;
		set_number(currAddr, SET_START_ADDRESS);
		set_mode(GBA_READ_EEPROM);
		
		uint8_t eepromFirstBuffer[0x200];
		while (currAddr < endAddr) {
			com_read_bytes(READ_BUFFER, 8);
			memcpy(&eepromFirstBuffer[currAddr], readBuffer, 8);
			
			currAddr += 8;
			
			// Request 8 bytes more
			if (currAddr < endAddr) {
				com_read_cont();
			}
		}
		
		// Read second 512 bytes
		endAddr = 0x400;
		com_read_cont();
		
		uint8_t eepromSecondBuffer[0x200];
		while (currAddr < endAddr) {
			com_read_bytes(READ_BUFFER, 8);
			memcpy(&eepromSecondBuffer[currAddr-0x200], readBuffer, 8);
			
			currAddr += 8;
			
			// Request 8 bytes more
			if (currAddr < endAddr) {
				com_read_cont();
			}
		}
		
		// Compare 512 bytes
		repeatedCount = 0;
		for (uint16_t c = 0; c < 0x200; c++) {
			if (eepromFirstBuffer[c] == eepromSecondBuffer[c]) {
				repeatedCount++;
			}
		}
		com_read_stop();
		
		if (repeatedCount >= 512) {
			return EEPROM_4KBIT; 
		}
		
		return EEPROM_64KBIT; 
	}	
}

// Read GBA game title (used for reading title when ROM mapping)
void gba_read_gametitle(void) {
	currAddr = 0x0000;
	endAddr = 0x00BF;
	set_number(currAddr, SET_START_ADDRESS);
	set_mode(GBA_READ_ROM);
	
	uint8_t startRomBuffer[385];
	while (currAddr < endAddr) {
		com_read_bytes(READ_BUFFER, 64);
		memcpy(&startRomBuffer[currAddr], readBuffer, 64);
		currAddr += 64;
		
		if (currAddr < endAddr) {
			com_read_cont();
		}
	}
	com_read_stop();
	
	// Blank out game title
	for (uint8_t b = 0; b < 16; b++) {
		gameTitle[b] = 0;
	}
	// Read cartridge title and check for non-printable text
	for (uint16_t titleAddress = 0xA0; titleAddress <= 0xAB; titleAddress++) {
		char headerChar = startRomBuffer[titleAddress];
		if ((headerChar >= 0x30 && headerChar <= 0x39) || // 0-9
			 (headerChar >= 0x41 && headerChar <= 0x5A) || // A-Z
			 (headerChar >= 0x61 && headerChar <= 0x7A) || // a-z
			 (headerChar >= 0x24 && headerChar <= 0x29) || // #$%&'()
			 (headerChar == 0x2D) || // -
			 (headerChar == 0x2E) || // .
			 (headerChar == 0x5F) || // _
			 (headerChar == 0x20)) { // Space
			gameTitle[(titleAddress-0xA0)] = headerChar;
		}
		// Replace with an underscore
		else if (headerChar == 0x3A) { // :
			gameTitle[(titleAddress - 0xA0)] = '_';
		}
		else {
			gameTitle[(titleAddress-0xA0)] = '\0';
			break;
		}
	}
}

// Read the first 192 bytes of ROM, read the title, check and test for ROM, SRAM, EEPROM and Flash
int read_gba_header (void) {
	uint8_t logoCheck = 0;
	uint8_t startRomBuffer[385];
	
	currAddr = 0x0000;
	endAddr = 0x00BF;
	set_number(currAddr, SET_START_ADDRESS);
	set_mode(GBA_READ_ROM);
	
	while (currAddr < endAddr) {
		uint8_t comReadBytes = com_read_bytes(READ_BUFFER, 64);
		
		if (comReadBytes == 64) {
			memcpy(&startRomBuffer[currAddr], readBuffer, 64);
			currAddr += 64;
			
			// Request 64 bytes more
			if (currAddr < endAddr) {
				com_read_cont();
			}
		}
		else { // Didn't receive 64 bytes, usually this only happens for Apple MACs
			com_read_stop();
			delay_ms(500);
			
			// Flush buffer
			RS232_PollComport(cport_nr, readBuffer, 64);											
			
			// Start off where we left off
			set_number(currAddr / 2, SET_START_ADDRESS);
			set_mode(GBA_READ_ROM);	
		}
	}
	com_read_stop();
	
	logoCheck = 1;
	for (uint16_t logoAddress = 0x04; logoAddress <= 0x9F; logoAddress++) {
		if (nintendoLogoGBA[(logoAddress-0x04)] != startRomBuffer[logoAddress]) {
			logoCheck = 0;
		}
	}
	
	// Blank out game title
	for (uint8_t b = 0; b < 16; b++) {
		gameTitle[b] = 0;
	}
	// Read cartridge title and check for non-printable text
	for (uint16_t titleAddress = 0xA0; titleAddress <= 0xAB; titleAddress++) {
		char headerChar = startRomBuffer[titleAddress];
		if ((headerChar >= 0x30 && headerChar <= 0x57) || // 0-9
			 (headerChar >= 0x41 && headerChar <= 0x5A) || // A-Z
			 (headerChar >= 0x61 && headerChar <= 0x7A) || // a-z
			 (headerChar >= 0x24 && headerChar <= 0x29) || // #$%&'()
			 (headerChar == 0x2D) || // -
			 (headerChar == 0x2E) || // .
			 (headerChar == 0x5F) || // _
			 (headerChar == 0x20)) { // Space
			gameTitle[(titleAddress-0xA0)] = headerChar;
		}
		else {
			gameTitle[(titleAddress-0xA0)] = '\0';
			break;
		}
	}
	printf ("Game title: %s\n", gameTitle);
	
	
	// Nintendo Logo Check
	printf ("Logo check: ");
	if (logoCheck == 1) {
		printf ("OK\n");
		
		// ROM size
		printf ("Calculating ROM size");
		romSize = gba_check_rom_size();
		
		// EEPROM check
		ramSize = 0;
		printf ("\nChecking for EEPROM");
		
		// Check if we have a Intel flash cart, if so, skip the EEPROM check as it can interfer with reading the last 2MB of the ROM
		if (gbxcartFirmwareVersion >= 10) {
			if (gba_detect_intel_flash_cart() == FLASH_FOUND_INTEL) {
				printf("... Skipping, Intel Flash cart detected");
				eepromSize = 0;
			}
			else {
				eepromSize = gba_check_eeprom();
			}
		}
		else {
			eepromSize = gba_check_eeprom();
		}
		
		// SRAM/Flash check/size, if no EEPROM present
		if (eepromSize == 0 && ramSize == 0) {
			printf ("\nCalculating SRAM/Flash size");
			ramSize = gba_check_sram_flash();
		}
		
		// If file exists, we know the ram has been erased before, so read memory info from this file
		load_cart_ram_info();
		
		// Print out
		printf ("\nROM size: %iMByte\n", romSize);
		romEndAddr = ((1024 * 1024) * romSize);
		
		if (hasFlashSave >= 2) {
			printf("Flash size: ");
		}
		else if (hasFlashSave == NO_FLASH) {
			printf("SRAM size: ");
		}
		else {
			printf("SRAM/Flash size: ");
		}
		
		if (ramSize == 0) {
			ramEndAddress = 0;
			printf ("None\n");
		}
		else if (ramSize == 1) {
			ramEndAddress = 0x8000;
			ramBanks = 1;
			printf ("256Kbit\n");
		}
		else if (ramSize == 2) {
			ramEndAddress = 0x10000;
			ramBanks = 1;
			printf ("512Kbit\n");
		}
		else if (ramSize == 3) {
			ramEndAddress = 0x10000;
			ramBanks = 2;
			printf ("1Mbit\n");
		}
		
		printf ("EEPROM: ");
		if (eepromSize == EEPROM_NONE) {
			eepromEndAddress = 0;
			printf ("None\n");
		}
		else if (eepromSize == EEPROM_4KBIT) {
			eepromEndAddress = 0x200;
			printf ("4Kbit\n");
		}
		else if (eepromSize == EEPROM_64KBIT) {
			eepromEndAddress = 0x2000;
			printf ("64Kbit\n");
		}
	}
	else {
		printf ("Failed\nSkipping ROM/RAM checks.\n");
	}
	return logoCheck;
}


// ****** GBA Cart Flasher functions ******

// Check for Intel based flash carts
uint8_t gba_detect_intel_flash_cart(void) {
	// Set to reading mode
	gba_flash_write_address_byte(0x00, 0xFF);
	delay_ms(5);
	
	// Read rom a tiny bit before writing
	set_number(0x00, SET_START_ADDRESS);
	set_mode(GBA_READ_ROM);
	com_read_bytes(READ_BUFFER, 64);
	com_read_stop();
	
	// Flash ID command
	gba_flash_write_address_byte(0x00, 0x90);
	delay_ms(1);
	
	// Read ID
	set_number(0x00, SET_START_ADDRESS);
	set_mode(GBA_READ_ROM);
	com_read_bytes(READ_BUFFER, 64);
	com_read_stop(); // End read
	
	// Check Manufacturer/Chip ID
	if ((readBuffer[0] == 0x8A && readBuffer[1] == 0 && readBuffer[2] == 0x15 && readBuffer[3] == 0x88) ||
		 (readBuffer[0] == 0x20 && readBuffer[1] == 0 && readBuffer[2] == 0xC4 && readBuffer[3] == 0x88)) {
		
		// Back to reading mode
		gba_flash_write_address_byte(currAddr, 0xFF);
		delay_ms(5);
		
		return FLASH_FOUND_INTEL;
	}
	else {
		// Back to reading mode
		gba_flash_write_address_byte(currAddr, 0xFF);
		delay_ms(5);
		
		return 0;
	}
}

// GBA Flash Cart, write address and byte
void gba_flash_write_address_byte (uint32_t address, uint16_t byte) {
	// Divide address by 2 as one address has 16 bytes of data
	address /= 2;
	
	char AddrString[20];
	sprintf(AddrString, "%c%x", 'n', address);
	RS232_cputs(cport_nr, AddrString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	
	char byteString[15];
	sprintf(byteString, "%c%x", 'n', byte);
	RS232_cputs(cport_nr, byteString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	
	com_wait_for_ack();
}


// Select which pin need to pulse as WE (Audio or WR)
void gb_flash_pin_setup(char pin) {
	set_mode(GB_FLASH_WE_PIN);
	set_mode(pin);
}

// Write address and byte to flash
void gb_flash_write_address_byte (uint16_t address, uint8_t byte) {
	char AddrString[15];
	sprintf(AddrString, "%c%x", 'F', address);
	RS232_cputs(cport_nr, AddrString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	delay_ms(5);
	
	char byteString[15];
	sprintf(byteString, "%x", byte);
	RS232_cputs(cport_nr, byteString);
	RS232_SendByte(cport_nr, 0);
	RS232_drain(cport_nr);
	delay_ms(5);
	
	com_wait_for_ack(); 
}