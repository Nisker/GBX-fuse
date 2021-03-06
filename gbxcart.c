/*
 Author: Nisker
 Credits to: Alex from insideGadgets (www.insidegadgets.com) for the initial implementation.
 Created: 14/11/2021
 Last Modified: 29/01/2022
 License: GPL-3.0

 */

#include "gbxcart.h"

unsigned int save_reserved_mem = 0;
unsigned int game_reserved_mem = 0;
char dumped_name[20];

pthread_mutex_t mutex;
pthread_cond_t cond;

static void allocate(char **ptr, unsigned int *prevSize, unsigned int size){
	if (*prevSize < size) {
		if (prevSize) *ptr = (char *) malloc(size);
		else *ptr = (char *) realloc(*ptr, size);
		*prevSize = size;
	}
}

int gba(){
	read_config();
	
	// Open COM port
	if (com_test_port() == 0) {
		read_one_letter();
		return 1;
	}

	// Break out of any existing functions on ATmega
	set_mode('0');
	RS232_flushRX(cport_nr);
	
	// Get PCB version
	gbxcartPcbVersion = request_value(READ_PCB_VERSION);
	xmas_wake_up();
	
	if (request_value(READ_FIRMWARE_VERSION) == 0) {
		printf("\nFirmware L1 may be installed. In order to use this applications you will need to downgrade to R30.\n");
		read_one_letter();
		return 1;
	}

	// GBx v1.4 - Power up the cart if not already powered up and flush buffer
	gbx_cart_power_up();
	RS232_flushRX(cport_nr);
	
	return 0;
}

static int dumpRam() {
	printf("\n--- Backup save from Cartridge to PC---\n");
	if (cartridgeMode == GB_MODE) {
		// Does cartridge have RAM
		if (ramEndAddress > 0 && headerCheckSumOk == 1) {
			allocate(&dmp_save.data, &save_reserved_mem, ramEndAddress);
			currAddr = 0x00000;

			mbc2_fix();
			if (cartridgeType <= 4) { // MBC1
				set_bank(0x6000, 1); // Set RAM Mode
			}
			set_bank(0x0000, 0x0A); // Initialise MBC

			// Check if Gameboy Camera cart with v1.0/1.1 PCB with R1 firmware, read data slower
			if (cartridgeType == 252 && gbxcartFirmwareVersion == 1) {
				// Read RAM
				for (uint8_t bank = 0; bank < ramBanks; bank++) {
					uint16_t ramAddress = 0xA000;
					set_bank(0x4000, bank);
					set_number(ramAddress, SET_START_ADDRESS); // Set start address again

					RS232_cputs(cport_nr, "M0"); // Disable CS/RD/WR/CS2-RST from going high after each command
					RS232_drain(cport_nr);
					delay_ms(5);

					set_mode(GB_CART_MODE);

					while (ramAddress < ramEndAddress) {
						for (uint8_t x = 0; x < 64; x++) {

							char hexNum[7];
							sprintf(hexNum, "HA0x%x", ((ramAddress+x) >> 8));
							RS232_cputs(cport_nr, hexNum);
							RS232_SendByte(cport_nr, 0);
							RS232_drain(cport_nr);

							sprintf(hexNum, "HB0x%x", ((ramAddress+x) & 0xFF));
							RS232_cputs(cport_nr, hexNum);
							RS232_SendByte(cport_nr, 0);
							RS232_drain(cport_nr);

							RS232_cputs(cport_nr, "LD0x60"); // cs_mreqPin_low + rdPin_low
							RS232_SendByte(cport_nr, 0);
							RS232_drain(cport_nr);

							RS232_cputs(cport_nr, "DC");
							RS232_drain(cport_nr);

							RS232_cputs(cport_nr, "HD0x60"); // cs_mreqPin_high + rdPin_high
							RS232_SendByte(cport_nr, 0);
							RS232_drain(cport_nr);

							RS232_cputs(cport_nr, "LA0xFF");
							RS232_SendByte(cport_nr, 0);
							RS232_drain(cport_nr);

							RS232_cputs(cport_nr, "LB0xFF");
							RS232_SendByte(cport_nr, 0);
							RS232_drain(cport_nr);
						}

						com_read_bytes(NULL, 64);
						memcpy(dmp_save.data+currAddr, readBuffer, 64);
						currAddr += 64;
						ramAddress += 64;

						// Request 64 bytes more
						if (ramAddress < ramEndAddress) {
							com_read_cont();
						}
					}
					com_read_stop(); // Stop reading RAM (as we will bank switch)
				}

				RS232_cputs(cport_nr, "M1");
				RS232_drain(cport_nr);
			}

			else {
				if (ramEndAddress == 0xA1FF) xmas_setup(ramEndAddress / 28);
				else if (ramEndAddress == 0xA7FF) xmas_setup(ramEndAddress / 4 / 28);
				else xmas_setup((ramBanks * (ramEndAddress - 0xA000 + 1)) / 28);

				// Read RAM
				for (uint8_t bank = 0; bank < ramBanks; bank++) {
					uint16_t ramAddress = 0xA000;
					set_bank(0x4000, bank);
					set_number(ramAddress, SET_START_ADDRESS); // Set start address again
					set_mode(READ_ROM_RAM); // Set rom/ram reading mode

					while (ramAddress < ramEndAddress) {
						uint8_t comReadBytes = com_read_bytes(NULL, 64);
						if (comReadBytes == 64) {
							memcpy(dmp_save.data+currAddr, readBuffer, comReadBytes);
							currAddr += 64;
							ramAddress += 64;

							// Request 64 bytes more
							if (ramAddress < ramEndAddress) {
								com_read_cont();
							}
						}
						else { // Didn't receive 64 bytes, usually this only happens for Apple MACs
							com_read_stop();
							delay_ms(500);

							// Flush buffer
							RS232_PollComport(cport_nr, readBuffer, 64);											

							// Start off where we left off
							set_number(ramAddress, SET_START_ADDRESS);
							set_mode(READ_ROM_RAM);				
						}
					}
					com_read_stop(); // Stop reading RAM (as we will bank switch)
				}
			}

			set_bank(0x4000, 0x00); // Stop rumble if it's present
			set_bank(0x0000, 0x00); // Disable RAM

			gbx_set_done_led();
		}
		else {
			gbx_set_error_led();
			return 1;
		}
	}
	else { // GBA mode
		// Does cartridge have RAM
		if (ramEndAddress > 0 || eepromEndAddress > 0) {
			// SRAM/Flash
			if (ramEndAddress > 0) {
				allocate(&dmp_save.data, &save_reserved_mem, ramEndAddress);
				xmas_setup((ramBanks * ramEndAddress) / 28);

				// Read RAM
				for (uint8_t bank = 0; bank < ramBanks; bank++) {
					// Flash, switch bank 1
					if (hasFlashSave >= FLASH_FOUND && bank == 1) {
						set_number(1, GBA_FLASH_SET_BANK);
					}
					else if (hasFlashSave == NO_FLASH && bank == 1) { // 1Mbit SRAM
						gba_flash_write_address_byte(0x1000000, 0x1);
					}

					// Set start and end address
					currAddr = 0x00000;
					endAddr = ramEndAddress;
					set_number(currAddr, SET_START_ADDRESS);
					set_mode(GBA_READ_SRAM);

					while (currAddr < endAddr) {
						uint8_t comReadBytes = com_read_bytes(NULL, 64);
						if (comReadBytes == 64) {
							memcpy(dmp_save.data+currAddr, readBuffer, comReadBytes);
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
							set_mode(GBA_READ_SRAM);				
						}
						led_progress_percent(currAddr, (ramBanks * ramEndAddress) / 28);
					}

					com_read_stop(); // End read (for bank if flash)

					// Flash, switch back to bank 0
					if (hasFlashSave >= FLASH_FOUND && bank == 1) {
						set_number(0, GBA_FLASH_SET_BANK);
					}
					// SRAM 1Mbit, switch back to bank 0
					else if (hasFlashSave == NO_FLASH && bank == 1) {
						gba_flash_write_address_byte(0x1000000, 0x0);
					}
				}
			}

			// EEPROM
			else {
				allocate(&dmp_save.data, &save_reserved_mem, eepromEndAddress);
				xmas_setup(eepromEndAddress / 28);
				set_number(eepromSize, GBA_SET_EEPROM_SIZE);

				// Set start and end address
				currAddr = 0x000;
				endAddr = eepromEndAddress;
				set_number(currAddr, SET_START_ADDRESS);
				set_mode(GBA_READ_EEPROM);

				// Read EEPROM
				while (currAddr < endAddr) {
					com_read_bytes(NULL, 8);
					memcpy(dmp_save.data+currAddr, readBuffer, 8);
					currAddr += 8;

					// Request 8 bytes more
					if (currAddr < endAddr) com_read_cont();
					led_progress_percent(currAddr, endAddr / 28);
				}
				com_read_stop(); // End read
			}
			gbx_set_done_led();
		}
		else {
			gbx_set_error_led();
			return 1;
		}
	}
	if (options.filename) strcpy(dmp_save.name, options.filename);
	else strcpy(dmp_save.name, gameTitle);
	strcat(dmp_save.name, ".sav");
	dmp_save.size = currAddr;
	return 0;
}

static int writeRam() {
	printf("\n--- Restore save from PC to Cartridge ---\n");
	if (cartridgeMode == GB_MODE) {
		// Does cartridge have RAM
		if (ramEndAddress > 0 && headerCheckSumOk == 1) {
			mbc2_fix();
			if (cartridgeType <= 4) { // MBC1
				set_bank(0x6000, 1); // Set RAM Mode
			}
			set_bank(0x0000, 0x0A); // Initialise MBC
			
			if (ramEndAddress == 0xA1FF) xmas_setup(ramEndAddress / 28);
			else if (ramEndAddress == 0xA7FF) xmas_setup(ramEndAddress / 4 / 28);
			else xmas_setup((ramBanks * (ramEndAddress - 0xA000 + 1)) / 28);
			
			
			// Write RAM
			uint32_t readBytes = 0;
			for (uint8_t bank = 0; bank < ramBanks; bank++) {
				uint16_t ramAddress = 0xA000;
				set_bank(0x4000, bank);
				set_number(0xA000, SET_START_ADDRESS); // Set start address again
				
				while (ramAddress < ramEndAddress) {
					memcpy(&writeBuffer, dmp_save.data+readBytes, 64);
					com_write_bytes_from_file(WRITE_RAM, NULL, 64);
					ramAddress += 64;
					readBytes += 64;
					com_wait_for_ack();
					
					// Print progress
					if (ramEndAddress == 0xA1FF) led_progress_percent(readBytes, 28);
					else if (ramEndAddress == 0xA7FF) led_progress_percent(readBytes / 4, 28);
					else led_progress_percent(readBytes, (ramBanks * (ramEndAddress - 0xA000 + 1)) / 28);
				}
			}
			set_bank(0x4000, 0x00); // Stop rumble if it's present
			set_bank(0x0000, 0x00); // Disable RAM
			
			gbx_set_done_led();
			printf("\nFinished\n");
			return 0;
		}
		else printf("Cartridge has no RAM\n");
	}
	else { // GBA mode
		// Does cartridge have RAM
		if (ramEndAddress > 0 || eepromEndAddress > 0) {
			// SRAM/Flash or EEPROM
			if (eepromSize == EEPROM_NONE) {
				// Check if it's SRAM or Flash (if we haven't checked before)
				if (hasFlashSave == NOT_CHECKED) hasFlashSave = gba_test_sram_flash_write();

				if (hasFlashSave >= FLASH_FOUND) printf("Going to write save to Flash from %s", gameTitle);
				else printf("Going to write save to SRAM from %s", gameTitle);
			}
			else printf("Going to write save to EEPROM from %s", gameTitle);

			if (eepromSize == EEPROM_NONE) {
				if (hasFlashSave >= FLASH_FOUND) printf("\nWriting Save to Flash from %s", gameTitle);
				else printf("\nWriting Save to SRAM from %s", gameTitle);
			}
			else printf("\nWriting Save to EEPROM from %s", gameTitle);

			// SRAM
			if (hasFlashSave == NO_FLASH_SRAM_FOUND && eepromSize == EEPROM_NONE) {
				xmas_setup((ramEndAddress * ramBanks) / 28);
				
				uint32_t readBytes = 0;
				for (uint8_t bank = 0; bank < ramBanks; bank++) {
					if (bank == 1) { // 1Mbit SRAM
						gba_flash_write_address_byte(0x1000000, 0x1);
					}
					
					// Set start and end address
					currAddr = 0x0000;
					endAddr = ramEndAddress;
					set_number(currAddr, SET_START_ADDRESS);
					
					// Write
					while (currAddr < endAddr) {
						memcpy(&writeBuffer, dmp_save.data+readBytes, 64);
						com_write_bytes_from_file(GBA_WRITE_SRAM, NULL, 64);
						currAddr += 64;
						readBytes += 64;
						com_wait_for_ack();
						
						led_progress_percent(readBytes, ramEndAddress * ramBanks / 28);
					}
					
					// SRAM 1Mbit, switch back to bank 0
					if (bank == 1) {
						gba_flash_write_address_byte(0x1000000, 0x0);
					}
				}
			}
			
			// EEPROM
			else if (eepromSize != EEPROM_NONE) {
				xmas_setup(eepromEndAddress / 28);
				set_number(eepromSize, GBA_SET_EEPROM_SIZE);
				
				// Set start and end address
				currAddr = 0x000;
				endAddr = eepromEndAddress;
				set_number(currAddr, SET_START_ADDRESS);
				
				// Write
				uint32_t readBytes = 0;
				while (currAddr < endAddr) {
					memcpy(&writeBuffer, dmp_save.data+readBytes, 8);
					com_write_bytes_from_file(GBA_WRITE_EEPROM, NULL, 8);
					currAddr += 8;
					readBytes += 8;
					
					// Wait for ATmega to process write (~320us) and for EEPROM to write data (6ms)
					com_wait_for_ack();
					led_progress_percent(readBytes, endAddr / 28);
				}
			}
			
			// Flash
			else if (hasFlashSave != NO_FLASH) {
				xmas_setup((ramBanks * endAddr) / 28);
				
				uint32_t readBytes = 0;
				for (uint8_t bank = 0; bank < ramBanks; bank++) {
					// Set start and end address
					currAddr = 0x0000;
					endAddr = ramEndAddress;
					set_number(currAddr, SET_START_ADDRESS);
					
					// Program flash in 128 bytes at a time
					if (hasFlashSave == FLASH_FOUND_ATMEL) {
						while (currAddr < endAddr) {
							memcpy(&writeBuffer, dmp_save.data+readBytes, 128);
							com_write_bytes_from_file(GBA_FLASH_WRITE_ATMEL, NULL, 128);
							currAddr += 128;
							readBytes += 128;
							com_wait_for_ack(); // Wait for write complete
							led_progress_percent(readBytes, (ramBanks * endAddr)  / 28);
						}
					}
					else { // Program flash in 1 byte at a time
						if (bank == 1) {
							set_number(1, GBA_FLASH_SET_BANK); // Set bank 1
						}
						uint8_t sector = 0;
						while (currAddr < endAddr) {
							if (currAddr % 4096 == 0) {
								printf("erase sector\n");
								flash_4k_sector_erase(sector);
								sector++;
								com_wait_for_ack(); // Wait 25ms for sector erase
								
								// Wait for first byte to be 0xFF, that's when we know the sector has been erased
								readBuffer[0] = 0;
								while (readBuffer[0] != 0xFF) {
									set_number(currAddr, SET_START_ADDRESS);
									set_mode(GBA_READ_SRAM);
									
									com_read_bytes(READ_BUFFER, 64);
									com_read_stop();
									
									if (readBuffer[0] != 0xFF) {
										delay_ms(5);
									}
								}
								// Set start address again
								set_number(currAddr, SET_START_ADDRESS);
								delay_ms(5); // Wait a little bit as hardware might not be ready
							}
							memcpy(&writeBuffer, dmp_save.data+readBytes, 64);
							com_write_bytes_from_file(GBA_FLASH_WRITE_BYTE, NULL, 64);
							currAddr += 64;
							readBytes += 64;
							com_wait_for_ack(); // Wait for write complete
							led_progress_percent(readBytes, (ramBanks * endAddr)  / 28);
						}
					}
					
					if (bank == 1) {
						set_number(0, GBA_FLASH_SET_BANK); // Set bank 0 again
					}
				}
			}
			gbx_set_done_led();
			printf("\nFinished\n");
			return 0;
		}
		else {
			printf("Cartridge has no RAM\n");
			return 1;
		}
	}
	return 1;
}

static void dumpRom() {
	printf("Reading ROM: %s\n", gameTitle);
	if (cartridgeMode == GB_MODE) {
		// Set start and end address
		uint32_t ramAddr = 0x0000;
		currAddr = 0x0000;
		endAddr = 0x7FFF;
		xmas_setup((romBanks * 16384) / 28);
		romSize < 8 ? 
		allocate(&dmp.data, &game_reserved_mem, 0x8000<<romSize): 
		allocate(&dmp.data, &game_reserved_mem, 0x8000<<8);
					
		// Read ROM
		uint16_t timedoutCounter = 0;
		for (uint16_t bank = 1; bank < romBanks; bank++) {				
			if (cartridgeType >= 5) { // MBC2 and above
				if (bank >= 256) {
					set_bank(0x3000, 1); // High bit
				}
				else {
					set_bank(0x3000, 0); // High bit
				}
				set_bank(0x2100, bank & 0xFF);
			}
			else if (cartridgeType >= 1) { // MBC1
				if ((strncmp(gameTitle, "MOMOCOL", 7) == 0) || (strncmp(gameTitle, "BOMCOL", 6) == 0)) { // MBC1 Hudson
					set_bank(0x4000, bank >> 4);
					if (bank < 10) {
						set_bank(0x2000, bank & 0x1F);
					}
					else {
						set_bank(0x2000, 0x10 | (bank & 0x1F));
					}
				}
				else { // Regular MBC1
					set_bank(0x6000, 0); // Set ROM Mode 
					set_bank(0x4000, bank >> 5); // Set bits 5 & 6 (01100000) of ROM bank
					set_bank(0x2000, bank & 0x1F); // Set bits 0 & 4 (00011111) of ROM bank
				}
			}

			if (bank > 1) { currAddr = 0x4000; }

			// Set start address and rom reading mode
			set_number(currAddr, SET_START_ADDRESS);
			if (fastReadEnabled == 1) {
				set_mode(READ_ROM_4000H);
			}
			else {
				set_mode(READ_ROM_RAM);
			}
			// Read data
			uint8_t localbuffer[257];
			while (currAddr < endAddr) {
				if (fastReadEnabled == 1) {
					uint8_t rxBytes = RS232_PollComport(cport_nr, localbuffer, 64);
					if (rxBytes > 0) {
						localbuffer[rxBytes] = 0;
						memcpy(dmp.data+ramAddr, localbuffer, rxBytes);
						ramAddr += rxBytes;
						currAddr += rxBytes;
						timedoutCounter = 0;
					}
					else {
						timedoutCounter++;
						if (timedoutCounter >= 10000) { // Timed out, restart transfer 1 bank before
							timedoutCounter = 0;
							bank--;
							ramAddr -= (currAddr - 0x4000);
							break;			
						}
					}
					if (bank == 1 && fastReadEnabled == 1 && currAddr == 0x4000) { // Ask for another 32KB (only happens once)
						set_mode(READ_ROM_4000H);
					}
				}
				else {
					uint8_t comReadBytes = com_read_bytes(NULL, 64);
					if (comReadBytes == 64) {
						memcpy(dmp.data+ramAddr, readBuffer, comReadBytes);
						ramAddr += 64;
						currAddr += 64;

						// Request 64 bytes more
						if (currAddr < endAddr) {
							com_read_cont();
						}
					}
					else { // Didn't receive 64 bytes, usually this only happens for Apple MACs
						com_read_stop();
						delay_ms(500);
						printf("Retrying\n");

						// Flush buffer
						RS232_PollComport(cport_nr, readBuffer, 64);											

						// Start off where we left off
						set_number(currAddr, SET_START_ADDRESS);
						set_mode(READ_ROM_RAM);				
					}
				}
				led_progress_percent(ramAddr, (romBanks * 16384) / 28);
			}
			com_read_stop(); // Stop reading ROM (as we will bank switch)
		}
		currAddr = ramAddr;
	}
	else { // GBA mode
		// Set start and end address
		
		currAddr = 0x00000;
		endAddr = romEndAddr;
		set_number(currAddr, SET_START_ADDRESS);
		xmas_setup(endAddr / 28);
		allocate(&dmp.data, &game_reserved_mem, romEndAddr);

		// Fast reading
		if (fastReadEnabled == 1) {
			uint16_t timedoutCounter = 0;
			set_mode(GBA_READ_ROM_8000H);

			uint8_t buffer[65];
			while (currAddr < endAddr) {
				uint8_t rxBytes = RS232_PollComport(cport_nr, buffer, 64);
				if (rxBytes > 0) {
					buffer[rxBytes] = 0;
					memcpy(dmp.data+currAddr, buffer, rxBytes);
					currAddr += rxBytes;
					timedoutCounter = 0;
				}
				else {
					timedoutCounter++;
					if (timedoutCounter >= 10000) {
						timedoutCounter = 0;
						RS232_PollComport(cport_nr, readBuffer, 256); // Flush

						if (currAddr >= 0x20000) {
							uint32_t hexCalc = ((currAddr / 0x10000) - 1);
							uint32_t calculateRewind = 0x10000 * hexCalc;
							currAddr = calculateRewind;
							set_number(currAddr / 2, SET_START_ADDRESS);
						}
						else {
							currAddr = 0;
							set_mode('0');
							delay_ms(5);
							set_number(currAddr, SET_START_ADDRESS);
							delay_ms(5);
						}
					}
				}

				if (currAddr % 0x10000 == 0 && currAddr != endAddr) {
					set_mode(GBA_READ_ROM_8000H);
				}
				led_progress_percent(currAddr, endAddr / 28);
			}
			com_read_stop();
		}
		else {
			uint16_t readLength = 64;
			set_mode(GBA_READ_ROM);
			// Read data
			while (currAddr < endAddr) {
				uint8_t comReadBytes = com_read_bytes(NULL, readLength);
				
				if (comReadBytes == readLength) {
					memcpy(dmp.data+currAddr, readBuffer, readLength);
					currAddr += readLength;
					// Request 64 bytes more
					if (currAddr < endAddr) {
						com_read_cont();
					}
				}
				else { // Didn't receive 64 bytes
					com_read_stop();
					delay_ms(500);

					// Flush buffer
					RS232_PollComport(cport_nr, readBuffer, readLength);											

					set_number(currAddr / 2, SET_START_ADDRESS);
					set_mode(GBA_READ_ROM);				
				}
				led_progress_percent(currAddr, endAddr / 28);
			}
			com_read_stop();
		}
	}
	
	dmp.size = currAddr;
	gbx_set_done_led();
	strcpy(dumped_name, gameTitle);
}

static void CacheROM(){
	char cwd[200];
   	if (getcwd(cwd, sizeof(cwd)) != NULL) {
    	printf("Current working dir: %s\n", cwd);
	}
	char filename[20];
	strcpy(filename, nogame.name);
	cartridgeMode == GB_MODE? strcat(filename, ".gb") : strcat(filename, ".gba");
	if( access( filename, R_OK ) == 0 ) {
		printf("%s exists, reading it.\n", filename);
		FILE *fp = fopen(filename, "r");
		fseek(fp, 0, SEEK_END);
		unsigned int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		allocate(&dmp.data, &game_reserved_mem, size);
		fread(dmp.data, 1, size, fp);
		dmp.size = size;
		strcpy(dumped_name, nogame.name);
		fclose(fp);
	} else {
		printf("%s does not exist, ceating it.\n", filename);
		strcpy(nogame.name, "reading...");
		game = &nogame;
		dumpRom();
		FILE *fp = fopen(filename, "w+");
		fwrite(dmp.data, 1, dmp.size, fp);
		fclose(fp);
	}
}

static void updateTitle(){

	set_mode(VOLTAGE_3_3V);
	
	if (read_gba_header()) {
		strcpy(nogame.name, gameTitle);
		if (gbxcartPcbVersion == GBXMAS) xmas_set_leds(0x9AAA6AA);
		return;
	}
	if (read_gb_header()) {
		strcpy(nogame.name, gameTitle);
		if (gbxcartPcbVersion == GBXMAS) xmas_set_leds(0x6555955);	
		set_mode(VOLTAGE_5V);
		return;
	}
	strcpy(nogame.name, "no game");
}

void *Thandler(void *ptr) {
	struct fuse_session *se = (struct fuse_session*) ptr;
	struct timespec t;
	
    if (options.ramOnly) game = &ramOnlyFile;
	if (options.cache_path) {
		if( access( options.cache_path, F_OK ) == 0 ) {
			printf("%s exists.\n", options.cache_path);
			chdir(options.cache_path);
		} else {
			options.cache_path = NULL;
		}
	}

	while(!fuse_session_exited(se)){
		updateTitle();
		cartridgeMode = request_value(CART_MODE);

		if (strcmp(dumped_name, nogame.name)) {		// difference between dumped_name and nogame.name?
			save = &nosave;
			if (strcmp(nogame.name, "no game")) {	// did it read a game game?					
				if (!dumpRam()){
					save = &dmp_save;
                    notify_inode(SAVE_INO);
				}

                if (!options.ramOnly){
					if (options.cache_path) CacheROM();
					else {
						strcpy(nogame.name, "reading...");	
						game = &nogame;						
						notify_inode(GAME_INO);
						dumpRom();
					}
					if (options.filename) strcpy(dmp.name, options.filename);
					else strcpy(dmp.name, dumped_name);
					cartridgeMode == GB_MODE? strcat(dmp.name, ".gb") : strcat(dmp.name, ".gba");
                    game = &dmp;
                    notify_inode(GAME_INO);
                }
                else {
                    strcpy(dumped_name, gameTitle);
                }
                
			} else {
                if(!options.ramOnly) game = &nogame;	//it didnt read a game, set it to no game.
                if(options.reread) strcpy(dumped_name, "--invalid--");
			}
		} else if (game == &nogame) {
			save = &dmp_save;
            notify_inode(SAVE_INO);
            if(!options.ramOnly) {
				if (options.filename) strcpy(dmp.name, options.filename);
				else strcpy(dmp.name, dumped_name);
				cartridgeMode == GB_MODE? strcat(dmp.name, ".gb") : strcat(dmp.name, ".gba");
                game = &dmp;
                notify_inode(GAME_INO);
            }
		} else if (condition && !options.readonly){
			printf("I should write now\n");
			writeRam();
			condition = 0;
		}
		time(&t.tv_sec);
		t.tv_sec += 5;
		if (pthread_cond_timedwait(&cond, &mutex, &t) == 0)
		puts("Signal from fun_write()");
	}

	if (save_reserved_mem) free(dmp_save.data);
	if (game_reserved_mem) free(dmp.data);
	pthread_exit(NULL);
}