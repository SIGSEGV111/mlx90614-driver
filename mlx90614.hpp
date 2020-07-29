#ifndef _include_mlx90614_mlx90614_hpp_
#define _include_mlx90614_mlx90614_hpp_

#include <stdint.h>

namespace mlx90614
{
	extern bool DEBUG;

	struct config_reg_t
	{
		uint16_t
			iir : 3,
			repeat_test : 1,
			_wtf1 : 2,
			single_dual : 1,
			sign_ks : 1,
			fir : 3,
			gain : 3,
			sign_kt2 : 1,
			test_enabled : 1;
	};

	struct flag_reg_t
	{
		uint16_t
			_reserved1 : 4,
			init_por : 1,	//	LOW active
			ee_dead : 1,	//	HIGH active
			_reserved2 : 1,
			ee_busy : 1,	//	HIGH active
			_reserved3 : 8;
	};

	class TCRC8_X8X2X1X0
	{
		protected:
			uint8_t crc;

		public:
			void PutSlaveAddress(const uint8_t address, const bool read_mode);
			void PutDataByte(const uint8_t byte);

			uint8_t CRC() const { return this->crc; }

			void Reset() { this->crc = 0; }

			TCRC8_X8X2X1X0() : crc(0) {}
	};

	class TMLX90614
	{
		protected:
			const int fd_i2cbus;
			const uint8_t address;

			uint16_t Read(const uint8_t opcode, const uint8_t address);
			uint16_t ReadRam(const uint8_t address);
			uint16_t ReadEeprom(const uint8_t address);
			config_reg_t ReadConfig();
			flag_reg_t ReadFlags();
			void WriteEeprom(const uint8_t address, const uint16_t data);

		public:
			double t_ambient;
			double t_object1;
			double t_object2;

			void Refresh();
			void Reset();

			TMLX90614(const char* const i2c_bus_device, const uint8_t address);
			TMLX90614(const int fd_i2cbus, const uint8_t address);
			~TMLX90614();
	};
}

#endif
