// =============================================================================
// @author  Pontus Rodling <frigolit@frigolit.net>
// @license MIT license - See LICENSE for more information
// =============================================================================

#include <stdio.h>

#include "6301.h"
#include "hx20.h"
#include "hash.h"
#include "rtc.h"
#include "iocontroller.h"
#include "lcd.h"
#include "lcdcontroller.h"

CHX20::CHX20() {
	// Create controls
	controls = new CControls(this);

	// Initialize CPUs
	mcu_master = new C6301();
	#ifdef REALSLAVE
		mcu_slave = new C6301();
	#else
		mcu_slave = new FakeSlave();
	#endif
	
	// Link serial endpoints
	mcu_master->serial0->set_endpoint(mcu_slave->serial0);
	
	// Initialize keyboard
	keyboard_pressed = 0;
	keyboard_repeat = 0;
	
	// Initialize RTC
	rtc = new CRTC();
	
	// Initialize RAM
	ram0 = new CRAM(16128);
	ram1 = new CRAM(128);
	
	// Initialize I/O select
	ioctl = new CIOController();
	
	// Initialize ROMs
	for (int i = 0; i < 4; i++) {
		roms[i] = new CROM(8192);
	}
	optionrom = new CROM(8192);
	
	#ifdef REALSLAVE
		mcu_slave->maskrom = new CROM(4096);
		mcu_slave->maskrom->load_from_file((char *)"data/roms/test/slave.bin");
	#endif
	
	// Initialize LCD and controllers
	lcd = new CLCD();
	for (int i = 0; i < 6; i++) {
		lcd_ctls[i] = new CLCDController();
		lcd_ctls[i]->set_lcd(lcd, (i % 3) * 40, (i / 3) * 16);
	}

	// Add master MCU memory devices
	mcu_master->membus->add(ioctl,    0x0020, 32,    0x0020);
	mcu_master->membus->add(rtc,      0x0040, 64,    0x0000);
	mcu_master->membus->add(ram1,     0x0080, 128,   0x0000);
	mcu_master->membus->add(ram0,     0x0100, 16128, 0x0000);

	mcu_master->membus->add(roms[3],  0x8000, 8192,  0x0000);
	mcu_master->membus->add(roms[2],  0xA000, 8192,  0x0000);
	mcu_master->membus->add(roms[1],  0xC000, 8192,  0x0000);
	mcu_master->membus->add(roms[0],  0xE000, 8192,  0x0000);

	mcu_master->membus->add(optionrom, 0x6000, 8192,  0x0000);

	// Attach hardware to I/O controller
	for (int i = 0; i < 6; i++) {
		ioctl->set_lcd_controller(i, lcd_ctls[i]);
	}
	
	// Checksum ROMs
	
	#ifdef REALSLAVE
		CHash *hash = new CHash();
		uint8_t buf[4096];

		for (int n = 0; n < 4096; n++) {
			buf[n] = mcu_slave->maskrom->read(n);
		}

		printf("Mask ROM - Checksum: %08X\n", hash->crc32(buf, 4096));
		delete hash;
	#endif
	
	printf("Master CPU reset vector is 0x%02X%02X\n", mcu_master->membus->read(0xFFFE), mcu_master->membus->read(0xFFFF));
	#ifdef REALSLAVE
		printf("Slave CPU reset vector is 0x%02X%02X\n", mcu_slave->maskrom->read(0x0FFE), mcu_slave->maskrom->read(0x0FFF));
	#endif

	// Reset
	reset();
}

void CHX20::load_roms(char *dirname) {
	char x[256];

	for (int i = 0; i < 4; i++) {
		sprintf(x, "data/roms/%s/rom%d.bin", dirname, i);
		roms[i]->load_from_file(x);
	}

	// Checksum ROMs
	CHash *hash = new CHash();
	uint8_t buf[8192];
	for (int i = 0; i < 4; i++) {
		for (int n = 0; n < 8192; n++) {
			buf[n] = roms[i]->read(n);
		}
		printf("ROM #%d - Checksum: %08X\n", i, hash->crc32(buf, 8192));
	}
	delete hash;
}

void CHX20::load_option_rom(char *path) {
	optionrom->load_from_file(path);

	// Checksum ROMs
	CHash *hash = new CHash();
	uint8_t buf[8192];
	for (int n = 0; n < 8192; n++) {
		buf[n] = optionrom->read(n);
	}
	printf("Option ROM - Checksum: %08X\n", hash->crc32(buf, 8192));
	delete hash;
}

void CHX20::keyboard_down(uint8_t c) {
	keyboard_pressed++;
	ioctl->keyboard_map[c] = 1;
}

void CHX20::keyboard_up(uint8_t c) {
	keyboard_pressed--;
	ioctl->keyboard_map[c] = 0;
}

CHX20::~CHX20() {
	delete(mcu_master);
	delete(mcu_slave);

	delete(rtc);
	delete(lcd);
	delete(ioctl);

	delete(ram0);
	delete(ram1);

	for (int i = 0; i < 4; i++) delete(roms[i]);
	delete(optionrom);

	for (int i = 0; i < 6; i++) {
		delete(lcd_ctls[i]);
	}
}

void CHX20::poweroff() {
	printf("not yet implemented: CHX20::poweroff()\n");
}

void CHX20::reset() {
	mcu_master->reset();
	mcu_slave->reset();
	ioctl->reset();
	
	// Set operating mode for CPUs
	mcu_master->set_port2(0x80);
	#ifdef REALSLAVE
		mcu_slave->set_port2(0xF0);
	#endif
}

void CHX20::think() {
	bool irq_power = false;
	bool irq_keyboard = keyboard_pressed > 0 && (ioctl->r_9g & 0x10);
	mcu_master->b_irq1 = irq_keyboard | irq_power;
	
	/*
	=== Port 1 ===
	P10    In     Data Set Ready (DSR)
	P11    In     Clear To Send (CTS)
	P12    Out    Slave CPU R/W control
	P13    In     External port interrupt (active low)
	P14    In     Power abnormal interrupt (active low)
	P15    In     Keyboard input interrupt (active low)
	P16    In     Peripheral status (Serial option, low = on)
	P17    In     Cartridge option flag (low = ROM, high = microcassette)
	
	Note: P13-15 means that the specified interrupt has triggered
	
	=== Port 2 ===
	P20    In     Barcode data
	P21    Out    Wired to CN2.2 (TXD)
	P22	   Out    Serial channel selection (4D, 0 = Slave CPU, 1 = Serial Port)
	P23    In     Serial receive (4D)
	P24    Out    Serial transmit (4D)
	P25    [In]	  Mode selection bit 0
	P26    [In]	  Mode selection bit 1
	P27    [In]	  Mode selection bit 2
	
	Note: P25-27 cannot be changed to outputs.
	*/
	
	bool b_rs232_dsr = false;
	bool b_rs232_cts = false;
	bool b_irq_ext = true;
	bool b_irq_pwr = !irq_power;
	bool b_irq_keyb = !irq_keyboard;
	bool b_opt_active = true;
	bool b_opt_type = false;
	
	mcu_master->set_port1((b_opt_type << 7) | (b_opt_active << 6) | (b_irq_keyb << 5) | (b_irq_pwr << 4) | (b_irq_ext << 3) | (b_rs232_cts << 1) | b_rs232_dsr);
	
	mcu_master->step();
	mcu_slave->step();
}

void CHX20::draw(SDL_Surface *dest, int x, int y) {
	lcd->draw(dest, x, y);
	controls->render(dest, x + 480, y);
}

