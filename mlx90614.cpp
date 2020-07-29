#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <endian.h>
#include "mlx90614.hpp"

#define SYSERR(expr) (([&](){ const auto r = ((expr)); if( (long)r == -1L ) { throw #expr; } else return r; })())

namespace mlx90614
{
	bool DEBUG = false;

 	static const uint16_t CRC8_POLY = (1<<8) + (1<<2) + (1<<1) + 1;

	void TCRC8_X8X2X1X0::PutSlaveAddress(const uint8_t address, const bool read_mode)
	{
		if(address >= 0x80)
			throw "address can not have the MSB set (7bit only)";

		this->PutDataByte((address << 1) | (read_mode ? 1 : 0));
	}

	void TCRC8_X8X2X1X0::PutDataByte(const uint8_t byte)
	{
		crc ^= byte;

		for (uint8_t bit = 8; bit > 0; --bit)
		{
			if (crc & 0x80)
			{
				crc = (crc << 1) ^ CRC8_POLY;
			}
			else
			{
				crc = (crc << 1);
			}
		}
	}

	uint16_t TMLX90614::Read(const uint8_t opcode, const uint8_t address)
	{
		if(address >= 32)
			throw "address must be in the range 0-31";

		const uint8_t cmd = opcode | (address & 0b00011111);
		if(DEBUG) fprintf(stderr, "[DEBUG] sending command = %02hhx (opcode = %02hhx, address = %02hhx)\n", cmd, opcode, address);

		union
		{
			uint8_t buffer[3];

			struct
			{
				uint16_t data;
				uint8_t crc;
			} __attribute__ ((packed));
		} __attribute__ ((packed)) rx;

		if(sizeof(rx) != 3)
			throw "logic error (struct is not 3 bytes in size)";

		i2c_msg messages[2];
		i2c_rdwr_ioctl_data rdwr;
		memset(messages, 0, sizeof(messages));
		memset(&rdwr, 0, sizeof(rdwr));

		messages[0].addr = this->address;
		messages[0].flags = 0;
		messages[0].len = 1;
		messages[0].buf = (unsigned char*)&cmd;

		messages[1].addr = this->address;
		messages[1].flags = I2C_M_RD;
		messages[1].len = 3;
		messages[1].buf = (unsigned char*)rx.buffer;

		rdwr.msgs = messages;
		rdwr.nmsgs = 2;

		SYSERR(ioctl(this->fd_i2cbus, I2C_RDWR, &rdwr));

		if(DEBUG) fprintf(stderr, "[DEBUG] rx-buffer: %02hhx %02hhx %02hhx\n", rx.buffer[0], rx.buffer[1], rx.buffer[2]);

		TCRC8_X8X2X1X0 expected_crc;
		expected_crc.PutSlaveAddress(this->address, false);
		expected_crc.PutDataByte(cmd);
		expected_crc.PutSlaveAddress(this->address, true);
		expected_crc.PutDataByte(rx.buffer[0]);
		expected_crc.PutDataByte(rx.buffer[1]);

		if(expected_crc.CRC() != rx.crc)
		{
			if(DEBUG) fprintf(stderr, "[DEBUG] expected CRC: %02hhx\n", expected_crc.CRC());
			if(DEBUG) fprintf(stderr, "[DEBUG] received CRC: %02hhx\n", rx.crc);
			throw "CRC mismatch";
		}
		else
		{
			if(DEBUG) fprintf(stderr, "[DEBUG] correct CRC (%02hhx)\n", expected_crc.CRC());
		}

		return rx.data;
	}

	uint16_t TMLX90614::ReadRam(const uint8_t address)
	{
		return Read(0b00000000, address);
	}

	uint16_t TMLX90614::ReadEeprom(const uint8_t address)
	{
		return Read(0b00100000, address);
	}

	config_reg_t TMLX90614::ReadConfig()
	{
		union
		{
			config_reg_t c;
			uint16_t d;
		} x;

		x.d = ReadEeprom(0x05);
		return x.c;
	}

	flag_reg_t TMLX90614::ReadFlags()
	{
		throw "not implemented yet";
// 		uint8_t cmd = 0xf0;
// 		if(SYSERR(write(this->fd_i2cbus, &cmd, 1)) != 1)
// 			throw "failed to send read flags command";
//
// 		uint8_t buffer[3];
// 		if(SYSERR(read(this->fd_i2cbus, buffer, 3)) != 3)
// 			throw "failed to read flags";
//
// // 		TCRC8_X8X2X1X0 expected_crc;
// // 		expected_crc.PutSlaveAddress(this->address, false);
// // 		expected_crc.PutDataByte(cmd);
// // 		expected_crc.PutSlaveAddress(this->address, true);
// // 		expected_crc.PutDataByte(buffer[0] & 0x0f);
// // // 		expected_crc.PutDataByte(buffer[1]);
// // 		if(expected_crc.CRC() != buffer[2])
// // 			throw "CRC mismatch while reading flags";
//
// 		flag_reg_t f;
// 		memcpy(&f, buffer, 2);
// 		return f;
	}

	void TMLX90614::WriteEeprom(const uint8_t, const uint16_t)
	{
		throw "not implemented yet";
	}

	static double ConvertTemp(const uint16_t raw)
	{
		return (double)raw * 0.02 - 273.15;
	}

	void TMLX90614::Refresh()
	{
		this->t_ambient = ConvertTemp(ReadRam(0x06));
		this->t_object1 = ConvertTemp(ReadRam(0x07));
		this->t_object2 = ConvertTemp(ReadRam(0x08));
	}

	void TMLX90614::Reset()
	{
		SYSERR(ioctl(this->fd_i2cbus, I2C_SLAVE, this->address));

		if((ReadEeprom(0x0E) & 0xff) != this->address)
			throw "i2c address mismatch between host and device";

		uint16_t id[4];
		for(uint8_t i = 0; i < 4; i++)
			id[i] = ReadEeprom(0x1C + i);

		fprintf(stderr, "[INFO] detected MLX90614 sensor @ 0x%hhX with chip-ID: %04hx.%04hx.%04hx.%04hx\n", this->address, id[0], id[1], id[2], id[3]);

		fprintf(stderr, "[INFO] Emissivity = x%lf\n", (double)ReadEeprom(0x04) / (double)0xffff);
		fprintf(stderr, "[INFO] ConfigReg = %04hx\n", ReadEeprom(0x05));
	}

	TMLX90614::TMLX90614(const char* const i2c_bus_device, const uint8_t address) : fd_i2cbus(SYSERR(open(i2c_bus_device, O_RDWR | O_CLOEXEC | O_SYNC))), address(address), t_ambient(NAN), t_object1(NAN), t_object2(NAN)
	{
		Reset();
	}

	TMLX90614::TMLX90614(const int fd_i2cbus, const uint8_t address) : fd_i2cbus(fd_i2cbus), address(address), t_ambient(NAN), t_object1(NAN), t_object2(NAN)
	{
		Reset();
	}

	TMLX90614::~TMLX90614()
	{
		close(this->fd_i2cbus);
	}
}
