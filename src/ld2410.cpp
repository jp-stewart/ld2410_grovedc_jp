/*
 *	An Arduino library for the Hi-Link LD2410 24Ghz FMCW radar sensor.
 *
 *  This sensor is a Frequency Modulated Continuous Wave radar, which makes it good for presence detection and its sensitivity at different ranges to both static and moving targets can be configured.
 *
 *	The code in this library is based off the manufacturer datasheet and reading of this initial piece of work for ESPHome https://github.com/rain931215/ESPHome-LD2410.
 *
 *	https://github.com/ncmreynolds/ld2410
 *
 *	Released under LGPL-2.1 see https://github.com/ncmreynolds/ld2410/LICENSE for full license
 *
 */
#ifndef ld2410_cpp
#define ld2410_cpp
#include "ld2410.h"


ld2410::ld2410()	//Constructor function
{
}

ld2410::~ld2410()	//Destructor function
{
}

bool ld2410::begin(Stream &radarStream, bool waitForRadar)	{
	radar_uart_ = &radarStream;		//Set the stream used for the LD2410
	if(debug_uart_ != nullptr)
	{
		debug_uart_->println(F("ld2410 started"));
	}
	if(waitForRadar)
	{
		if(debug_uart_ != nullptr)
		{
			debug_uart_->print(F("\nLD2410 firmware: "));
		}
		if(requestFirmwareVersion())
		{
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F(" v"));
				debug_uart_->print(firmware_major_version);
				debug_uart_->print('.');
				debug_uart_->print(firmware_minor_version);
				debug_uart_->print('.');
				debug_uart_->print(firmware_bugfix_version);
			}
			return true;
		}
		else
		{
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("no response"));
			}
		}
	}
	else
	{
		if(debug_uart_ != nullptr)
		{
			debug_uart_->print(F("\nLD2410 library configured"));
		}
		return true;
	}
	return false;
}

void ld2410::debug(Stream &terminalStream)
{
	debug_uart_ = &terminalStream;		//Set the stream used for the terminal
	#if defined(ESP8266)
	if(&terminalStream == &Serial)
	{
		  debug_uart_->write(17);			//Send an XON to stop the hung terminal after reset on ESP8266
	}
	#endif
}

bool ld2410::isConnected()
{
	if(millis() - radar_uart_last_packet_ < radar_uart_timeout)	//Use the last reading
	{
		return true;
	}
	if(read_frame_() && parse_frame_())	//Try and read a frame if the current reading is too old
	{
		return true;
	}
	return false;
}


bool ld2410::read()
{
	return read_frame_();
	return false;
}
bool ld2410::presenceDetected()
{
	return target_type_ != 0;
}
bool ld2410::stationaryTargetDetected()
{
	if(stationary_target_energy_ > 0)
	{
		return true;
	}
	return false;
}
uint16_t ld2410::stationaryTargetDistance()
{
	if(stationary_target_energy_ > 0)
	{
		return stationary_target_distance_;
	}
	return 0;
}
uint8_t ld2410::stationaryTargetEnergy()
{
	return stationary_target_energy_;
}

bool ld2410::movingTargetDetected()
{
	if(moving_target_energy_ > 0)
	{
		return true;
	}
	return false;
}
uint16_t ld2410::movingTargetDistance()
{
	if(moving_target_energy_ > 0)
	{
		return moving_target_distance_;
	}
	return 0;
}
uint8_t ld2410::movingTargetEnergy()
{
	return moving_target_energy_;
}

bool ld2410::read_frame_()
{
	if(radar_uart_ -> available())
	{
		if(frame_started_ == false)
		{
			uint8_t byte_read_ = radar_uart_ -> read();
			if(byte_read_ == 0xF4 || byte_read_ == 0xFD)
			{
				radar_data_frame_[radar_data_frame_position_++] = byte_read_;
				frame_started_ = true;
				#ifdef LD2410_DEBUG_FRAMES
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("\nLD2410 frame started: ."));
				}
				#endif
			}
		}
		else
		{
			if(radar_data_frame_position_ < LD2410_MAX_FRAME_LENGTH)
			{
				radar_data_frame_[radar_data_frame_position_++] = radar_uart_ -> read();
				#ifdef LD2410_DEBUG_FRAMES
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print('.');
				}
				#endif
				if(radar_data_frame_position_ > 7 &&
					(
					radar_data_frame_[0]                              == 0xF4 &&	//Data frames
					radar_data_frame_[1]                              == 0xF3 &&
					radar_data_frame_[2]                              == 0xF2 &&
					radar_data_frame_[3]                              == 0xF1 &&
					radar_data_frame_[radar_data_frame_position_ - 4] == 0xF8 &&
					radar_data_frame_[radar_data_frame_position_ - 3] == 0xF7 &&
					radar_data_frame_[radar_data_frame_position_ - 2] == 0xF6 &&
					radar_data_frame_[radar_data_frame_position_ - 1] == 0xF5
					) || (
					radar_data_frame_[0]                              == 0xFD &&	//Command frames
					radar_data_frame_[1]                              == 0xFC &&
					radar_data_frame_[2]                              == 0xFB &&
					radar_data_frame_[3]                              == 0xFA &&
					radar_data_frame_[radar_data_frame_position_ - 4] == 0x04 &&
					radar_data_frame_[radar_data_frame_position_ - 3] == 0x03 &&
					radar_data_frame_[radar_data_frame_position_ - 2] == 0x02 &&
					radar_data_frame_[radar_data_frame_position_ - 1] == 0x01
					)
					)
				{
					#ifdef LD2410_DEBUG_FRAMES
					if(debug_uart_ != nullptr)
					{
						debug_uart_->print(F("ended"));
					}
					#endif
					if(parse_frame_())
					{
						frame_started_ = true;
						radar_data_frame_position_ = 0;
						return true;
					}
					else
					{
						frame_started_ = false;
						radar_data_frame_position_ = 0;
					}
				}
			}
			else
			{
				#ifdef LD2410_DEBUG_FRAMES
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("\nLD2410 frame overran"));
				}
				#endif
				frame_started_ = false;
				radar_data_frame_position_ = 0;
			}
		}
	}
	return false;
}

void ld2410::print_frame_()
{
	if(debug_uart_ != nullptr)
	{
		debug_uart_->print(F("\nFrame: "));
		for(uint8_t i = 0; i < radar_data_frame_position_ ; i ++)
		{
			debug_uart_->print(radar_data_frame_[i],HEX);
			debug_uart_->print(' ');
		}
	}
}

bool ld2410::parse_frame_()
{
	if(			radar_data_frame_[0]                              == 0xF4 &&
				radar_data_frame_[1]                              == 0xF3 &&
				radar_data_frame_[2]                              == 0xF2 &&
				radar_data_frame_[3]                              == 0xF1 &&
				radar_data_frame_[radar_data_frame_position_ - 4] == 0xF8 &&
				radar_data_frame_[radar_data_frame_position_ - 3] == 0xF7 &&
				radar_data_frame_[radar_data_frame_position_ - 2] == 0xF6 &&
				radar_data_frame_[radar_data_frame_position_ - 1] == 0xF5)		//This is a data frame
	{
		uint16_t intra_frame_data_length_ = radar_data_frame_[4] + (radar_data_frame_[5] << 8);
		if(radar_data_frame_position_ == intra_frame_data_length_ + 10)
		{
			#ifdef LD2410_DEBUG_FRAMES
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nFrame payload: "));
				debug_uart_->print(intra_frame_data_length_);
				debug_uart_->print(F(" bytes"));
			}
			#endif
			if(radar_data_frame_[6] == 0x01 && radar_data_frame_[7] == 0xAA)	//Engineering mode data
			{
				target_type_ = radar_data_frame_[8];
				#ifdef LD2410_DEBUG_DATA
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("\nEngineering data: "));
					if(target_type_ == 0x00)
					{
						debug_uart_->print(F("no target"));
					}
					else if(target_type_ == 0x01)
					{
						debug_uart_->print(F("moving target - "));
					}
					else if(target_type_ == 0x02)
					{
						debug_uart_->print(F("stationary target - "));
					}
					else if(target_type_ == 0x03)
					{
						debug_uart_->print(F("moving & stationary targets - "));
					}
				}
				#endif
				/*
				 *
				 *	To-do support engineering mode
				 *
				 */
			}
			else if(intra_frame_data_length_ == 13 && radar_data_frame_[6] == 0x02 && radar_data_frame_[7] == 0xAA && radar_data_frame_[17] == 0x55 && radar_data_frame_[18] == 0x00)	//Normal target data
			{
				target_type_ = radar_data_frame_[8];
				moving_target_distance_ = radar_data_frame_[9] + (radar_data_frame_[10] << 8);
				moving_target_energy_ = radar_data_frame_[11];
				stationary_target_distance_ = radar_data_frame_[12] + (radar_data_frame_[13] << 8);
				stationary_target_energy_ = radar_data_frame_[14];
				detection_distance_ = radar_data_frame_[15] + (radar_data_frame_[16] << 8);
				#ifdef LD2410_DEBUG_DATA
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("\nNormal data: "));
					if(target_type_ == 0x00)
					{
						debug_uart_->print(F("no target"));
					}
					else if(target_type_ == 0x01)
					{
						debug_uart_->print(F("moving target - "));
					}
					else if(target_type_ == 0x02)
					{
						debug_uart_->print(F("stationary target - "));
					}
					else if(target_type_ == 0x03)
					{
						debug_uart_->print(F("moving & stationary targets - "));
					}
				}
				#endif
				#ifdef LD2410_DEBUG_DATA
				if(debug_uart_ != nullptr)
				{
					if(radar_data_frame_[8] & 0x01)
					{
						debug_uart_->print(F(" moving at "));
						debug_uart_->print(moving_target_distance_);
						debug_uart_->print(F("cm "));
						debug_uart_->print(F(" power "));
						debug_uart_->print(moving_target_energy_);
					}
					if(radar_data_frame_[8] & 0x02)
					{
						debug_uart_->print(F(" stationary at "));
						debug_uart_->print(stationary_target_distance_);
						debug_uart_->print(F("cm "));
						debug_uart_->print(F(" power "));
						debug_uart_->print(stationary_target_energy_);
					}
					if(radar_data_frame_[8] & 0x03)
					{
						debug_uart_->print(F(" detection at "));
						debug_uart_->print(detection_distance_);
						debug_uart_->print(F("cm"));
					}
				}
				#endif
				radar_uart_last_packet_ = millis();
				return true;
			}
			else
			{
				#ifdef LD2410_DEBUG_FRAMES
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("\nUnknown frame type"));
				}
				#endif
				print_frame_();
			}
		}
		else
		{
			#ifdef LD2410_DEBUG_FRAMES
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nFrame length unexpected: "));
				debug_uart_->print(radar_data_frame_position_);
				debug_uart_->print(F(" not "));
				debug_uart_->print(intra_frame_data_length_ + 10);
			}
			#endif
		}
	}
	else if(	radar_data_frame_[0]                              == 0xFD &&
				radar_data_frame_[1]                              == 0xFC &&
				radar_data_frame_[2]                              == 0xFB &&
				radar_data_frame_[3]                              == 0xFA &&
				radar_data_frame_[radar_data_frame_position_ - 4] == 0x04 &&
				radar_data_frame_[radar_data_frame_position_ - 3] == 0x03 &&
				radar_data_frame_[radar_data_frame_position_ - 2] == 0x02 &&
				radar_data_frame_[radar_data_frame_position_ - 1] == 0x01)		//This is an ACK/Command frame
	{
		#ifdef LD2410_DEBUG_FRAMES
		if(debug_uart_ != nullptr)
		{
			print_frame_();
		}
		#endif
		uint16_t intra_frame_data_length_ = radar_data_frame_[4] + (radar_data_frame_[5] << 8);
		#ifdef LD2410_DEBUG_FRAMES
		if(debug_uart_ != nullptr)
		{
			debug_uart_->print(F("\nACK frame payload: "));
			debug_uart_->print(intra_frame_data_length_);
			debug_uart_->print(F(" bytes"));
		}
		#endif
		latest_ack_ = radar_data_frame_[6];
		latest_command_success_ = (radar_data_frame_[8] == 0x00 && radar_data_frame_[9] == 0x00);
		if(intra_frame_data_length_ == 8 && latest_ack_ == 0xFF)
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nACK for entering configuration mode: "));
			}
			#endif
			if(latest_command_success_)
			{
				radar_uart_last_packet_ = millis();
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("OK"));
				}
				#endif
				return true;
			}
			else
			{
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("failed"));
				}
				return false;
			}
		}
		else if(intra_frame_data_length_ == 4 && latest_ack_ == 0xFE)
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nACK for leaving configuration mode: "));
			}
			#endif
			if(latest_command_success_)
			{
				radar_uart_last_packet_ = millis();
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("OK"));
				}
				#endif
				return true;
			}
			else
			{
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("failed"));
				}
				return false;
			}
		}
		else if(intra_frame_data_length_ == 28 && latest_ack_ == 0x61)
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nACK for current configuration: "));
			}
			#endif
			if(latest_command_success_)
			{
				radar_uart_last_packet_ = millis();
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("OK"));
				}
				#endif
				max_gate_distance = radar_data_frame_[11];
				max_moving_gate_distance = radar_data_frame_[12];
				max_stationary_gate_distance = radar_data_frame_[13];
				motion_sensitivity[0] = radar_data_frame_[14];
				motion_sensitivity[1] = radar_data_frame_[15];
				motion_sensitivity[2] = radar_data_frame_[16];
				motion_sensitivity[3] = radar_data_frame_[17];
				motion_sensitivity[4] = radar_data_frame_[18];
				motion_sensitivity[5] = radar_data_frame_[19];
				motion_sensitivity[6] = radar_data_frame_[20];
				motion_sensitivity[7] = radar_data_frame_[21];
				motion_sensitivity[8] = radar_data_frame_[22];
				stationary_sensitivity[0] = radar_data_frame_[23];
				stationary_sensitivity[1] = radar_data_frame_[24];
				stationary_sensitivity[2] = radar_data_frame_[25];
				stationary_sensitivity[3] = radar_data_frame_[26];
				stationary_sensitivity[4] = radar_data_frame_[27];
				stationary_sensitivity[5] = radar_data_frame_[28];
				stationary_sensitivity[6] = radar_data_frame_[29];
				stationary_sensitivity[7] = radar_data_frame_[30];
				stationary_sensitivity[8] = radar_data_frame_[31];
				sensor_idle_time_ = radar_data_frame_[32];
				sensor_idle_time_ += (radar_data_frame_[33] << 8);
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("\nMax gate distance: "));
					debug_uart_->print(max_gate_distance);
					debug_uart_->print(F("\nMax motion detecting gate distance: "));
					debug_uart_->print(max_moving_gate_distance);
					debug_uart_->print(F("\nMax stationary detecting gate distance: "));
					debug_uart_->print(max_stationary_gate_distance);
					debug_uart_->print(F("\nSensitivity per gate"));
					for(uint8_t i = 0; i < 9; i++)
					{
						debug_uart_->print(F("\nGate "));
						debug_uart_->print(i);
						debug_uart_->print(F(" ("));
						debug_uart_->print(i * 0.75);
						debug_uart_->print('-');
						debug_uart_->print((i+1) * 0.75);
						debug_uart_->print(F(" metres) Motion: "));
						debug_uart_->print(motion_sensitivity[i]);
						debug_uart_->print(F(" Stationary: "));
						debug_uart_->print(stationary_sensitivity[i]);
						
					}
					debug_uart_->print(F("\nSensor idle timeout: "));
					debug_uart_->print(sensor_idle_time_);
					debug_uart_->print('s');
				}
				#endif
				return true;
			}
			else
			{
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("failed"));
				}
				return false;
			}
		}
		else if(intra_frame_data_length_ == 4 && latest_ack_ == 0xA0)
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nACK for firmware version: "));
			}
			#endif
			if(latest_command_success_)
			{
				firmware_major_version = radar_data_frame_[7];
				firmware_minor_version = radar_data_frame_[8];
				firmware_bugfix_version = radar_data_frame_[9];
				radar_uart_last_packet_ = millis();
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("OK"));
				}
				#endif
				return true;
			}
			else
			{
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("failed"));
				}
				return false;
			}
		}
		else if(intra_frame_data_length_ == 4 && latest_ack_ == 0xA2)
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nACK for factory reset: "));
			}
			#endif
			if(latest_command_success_)
			{
				radar_uart_last_packet_ = millis();
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("OK"));
				}
				#endif
				return true;
			}
			else
			{
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("failed"));
				}
				return false;
			}
		}
		else if(intra_frame_data_length_ == 4 && latest_ack_ == 0xA3)
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nACK for restart: "));
			}
			#endif
			if(latest_command_success_)
			{
				radar_uart_last_packet_ = millis();
				#ifdef LD2410_DEBUG_ACKS
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("OK"));
				}
				#endif
				return true;
			}
			else
			{
				if(debug_uart_ != nullptr)
				{
					debug_uart_->print(F("failed"));
				}
				return false;
			}
		}
		else
		{
			#ifdef LD2410_DEBUG_ACKS
			if(debug_uart_ != nullptr)
			{
				debug_uart_->print(F("\nUnknown ACK"));
			}
			#endif
		}
	}
	return false;
}

void ld2410::send_command_preamble_()
{
	//Command preamble
	radar_uart_->write(char(0xFD));
	radar_uart_->write(char(0xFC));
	radar_uart_->write(char(0xFB));
	radar_uart_->write(char(0xFA));
}

void ld2410::send_command_postamble_()
{
	//Command end
	radar_uart_->write(char(0x04));
	radar_uart_->write(char(0x03));
	radar_uart_->write(char(0x02));
	radar_uart_->write(char(0x01));
}
bool ld2410::enter_configuration_mode_()
{
	send_command_preamble_();
	//Request firmware
	radar_uart_->write(char(0x04));	//Command is four bytes long
	radar_uart_->write(char(0x00));
	radar_uart_->write(char(0xFF));	//Request enter command mode
	radar_uart_->write(char(0x00));
	radar_uart_->write(char(0x01));
	radar_uart_->write(char(0x00));
	send_command_postamble_();
	radar_uart_last_command_ = millis();
	while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
	{
		read_frame_();
		if(latest_ack_ == 0xFF && latest_command_success_)
		{
			return true;
		}
	}
	return false;
}
bool ld2410::leave_configuration_mode_()
{
	send_command_preamble_();
	//Request firmware
	radar_uart_->write(char(0x02));	//Command is four bytes long
	radar_uart_->write(char(0x00));
	radar_uart_->write(char(0xFE));	//Request leave command mode
	radar_uart_->write(char(0x00));
	send_command_postamble_();
	radar_uart_last_command_ = millis();
	while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
	{
		read_frame_();
		if(latest_ack_ == 0xFE && latest_command_success_)
		{
			return true;
		}
	}
	return false;
}
bool ld2410::requestStartEngineeringMode()
{
	send_command_preamble_();
	//Request firmware
	radar_uart_->write(char(0x02));	//Command is four bytes long
	radar_uart_->write(char(0x00));
	radar_uart_->write(char(0x62));	//Request enter command mode
	radar_uart_->write(char(0x00));
	send_command_postamble_();
	radar_uart_last_command_ = millis();
	while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
	{
		read_frame_();
		if(latest_ack_ == 0x62 && latest_command_success_)
		{
			return true;
		}
	}
	return false;
}
bool ld2410::requestEndEngineeringMode()
{
	send_command_preamble_();
	//Request firmware
	radar_uart_->write(char(0x02));	//Command is four bytes long
	radar_uart_->write(char(0x00));
	radar_uart_->write(char(0x63));	//Request leave command mode
	radar_uart_->write(char(0x00));
	send_command_postamble_();
	radar_uart_last_command_ = millis();
	while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
	{
		read_frame_();
		if(latest_ack_ == 0x63 && latest_command_success_)
		{
			return true;
		}
	}
	return false;
}
bool ld2410::requestCurrentConfiguration()
{
	if(enter_configuration_mode_())
	{
		delay(50);
		send_command_preamble_();
		//Request firmware
		radar_uart_->write(char(0x02));	//Command is two bytes long
		radar_uart_->write(char(0x00));
		radar_uart_->write(char(0x61));	//Request current configuration
		radar_uart_->write(char(0x00));
		send_command_postamble_();
		radar_uart_last_command_ = millis();
		while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
		{
			read_frame_();
			if(latest_ack_ == 0x61 && latest_command_success_)
			{
				delay(50);
				leave_configuration_mode_();
				return true;
			}
		}
	}
	delay(50);
	leave_configuration_mode_();
	return false;
}
bool ld2410::requestFirmwareVersion()
{
	if(enter_configuration_mode_())
	{
		delay(50);
		send_command_preamble_();
		//Request firmware
		radar_uart_->write(char(0x02));	//Command is two bytes long
		radar_uart_->write(char(0x00));
		radar_uart_->write(char(0xA0));	//Request firmware version
		radar_uart_->write(char(0x00));
		send_command_postamble_();
		radar_uart_last_command_ = millis();
		while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
		{
			read_frame_();
			if(latest_ack_ == 0xA0 && latest_command_success_)
			{
				delay(50);
				leave_configuration_mode_();
				return true;
			}
		}
	}
	delay(50);
	leave_configuration_mode_();
	return false;
}
bool ld2410::requestRestart()
{
	if(enter_configuration_mode_())
	{
		delay(50);
		send_command_preamble_();
		//Request firmware
		radar_uart_->write(char(0x02));	//Command is two bytes long
		radar_uart_->write(char(0x00));
		radar_uart_->write(char(0xA3));	//Request restart
		radar_uart_->write(char(0x00));
		send_command_postamble_();
		radar_uart_last_command_ = millis();
		while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
		{
			read_frame_();
			if(latest_ack_ == 0xA3 && latest_command_success_)
			{
				delay(50);
				leave_configuration_mode_();
				return true;
			}
		}
	}
	delay(50);
	leave_configuration_mode_();
	return false;
}
bool ld2410::requestFactoryReset()
{
	if(enter_configuration_mode_())
	{
		delay(50);
		send_command_preamble_();
		//Request firmware
		radar_uart_->write(char(0x02));	//Command is two bytes long
		radar_uart_->write(char(0x00));
		radar_uart_->write(char(0xA2));	//Request factory reset
		radar_uart_->write(char(0x00));
		send_command_postamble_();
		radar_uart_last_command_ = millis();
		while(millis() - radar_uart_last_command_ < radar_uart_command_timeout_)
		{
			read_frame_();
			if(latest_ack_ == 0xA2 && latest_command_success_)
			{
				delay(50);
				leave_configuration_mode_();
				return true;
			}
		}
	}
	delay(50);
	leave_configuration_mode_();
	return false;
}

#endif
