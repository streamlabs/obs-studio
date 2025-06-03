/*
MIT License

Copyright (c) 2022-23 Corsair Memory, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "ElgatoUVCDevice.h"


#define WORKAROUND_HD60_S_PLUS_PAYLOAD_SIZE 1 //!<  Workaround HD60 S+ firmware issue: invalid payload length (seen with HDR and SPD info frames)


//==============================================================================
// # Elgato HID interface for UVC devices
//==============================================================================


enum class I2CAddress
{
	MCU					= 0x55,
};

//! @brief I2C registers for MCU (I2C address 0x55).
enum class MCU_I2C_REGISTER
{
	GET_HDR_PACKET		= 0x09, //!< HDR capable devices (HD60 S+, HD60 X)
	XET_HDR_TONEMAPPING	= 0x0A, //!< HDR capable devices (HD60 S+, HD60 X): Enable hardware tonemapping; param 0/1 (uint8_t)
};



//==============================================================================
// ## HID interface - I2C
//==============================================================================

//! @brief HID report case for new device type.
enum class REPORT_CASE_NEW
{
	REPORT_IIC_WRITE = 6,
	REPORT_IIC_READ = 7
};

//! @brief HID report IDs for new device type. Can also be queried via HidP_GetValueCaps()
enum class HID_REPORT_ID_NEW
{
	I2C_READ = 5,
	I2C_WRITE = 6
};

//! @brief HDI report IDs for original device type. Can also be queried via HidP_GetValueCaps()
enum class HID_REPORT_ID
{
	I2C_READ_SET_ID = 9,
	I2C_READ_GET_ID = 10,
	I2C_WRITE_ID = 11
};

const int I2C_BUFFER_HEADER_SIZE	=  4;
const int MAX_COMM_READ_BUFFER_SIZE	= 32;


//==============================================================================
// # Helpers
//==============================================================================

std::vector<EGAVDeviceID> GetElgatoUVCDeviceIDs()
{
	std::vector<EGAVDeviceID> devices {deviceIDHD60SPlus, deviceIDHD60X, deviceIDHD60XRev2 };
	// EXTEND_DEVICES
	return devices;
}

//! @return true for new USB chipset
bool IsNewDeviceType(const EGAVDeviceID& inDeviceID) { return (inDeviceID != deviceIDHD60SPlus); }



//==============================================================================
// # Class ElgatoUVCDevice
//==============================================================================

ElgatoUVCDevice::ElgatoUVCDevice(std::shared_ptr<EGAVHIDInterface> hid, bool isNewDeviceType) : mHIDImpl(hid), mNewDeviceType(isNewDeviceType) {}

EGAVResult ElgatoUVCDevice::ReadI2cData(uint8_t inI2CAddress, uint8_t inRegister, uint8_t* outData, uint8_t inLength)
{
	EGAVResult_CheckPointer(outData);
	EGAVResult_CheckPointer(mHIDImpl);

	EPL_ASSERT_BREAK(inLength <= MAX_COMM_READ_BUFFER_SIZE);

	const std::lock_guard<std::recursive_mutex> lock(mHIDMutex);

	EGAVResult res = EGAVResult::ErrUnknown;

	if (mNewDeviceType) 
	{
		const uint8_t writeLen = 1 /* +1 for byte register address*/, readLen = inLength, reportLen = 4 + writeLen + sizeof(readLen);
		std::vector<uint8_t> outputMessage{ reportLen, (uint8_t)REPORT_CASE_NEW::REPORT_IIC_READ, inI2CAddress, writeLen, inRegister, readLen };
		EPL_ASSERT_BREAK(reportLen == outputMessage.size());
		res = mHIDImpl->WriteHID(outputMessage, (int)HID_REPORT_ID_NEW::I2C_WRITE);
		if (res.Failed())
			error_printf("WriteHID() FAILED for I2C address 0x%02x, register 0x%02x", inI2CAddress, inRegister);
		else
		{
			std::vector<uint8_t> inputMessage;
			const int inputReportLength = 0xFF | ((int)REPORT_CASE_NEW::REPORT_IIC_READ << 8); // report case is coded into report length
			res = mHIDImpl->ReadHID(inputMessage, (int)HID_REPORT_ID_NEW::I2C_READ, inputReportLength);
			if (res.Failed())
				error_printf("ReadHID() FAILED for I2C address 0x%02x, register 0x%02x", inI2CAddress, inRegister);
			else
			{
				int dataLen = min((int)inLength, (int)(inputMessage.size() - 1));
				memcpy(outData, inputMessage.data() + 1, dataLen);
			}
		}
	}
	else
	{
		std::vector<uint8_t> outputMessage{ inI2CAddress, inRegister, inLength };
		res = mHIDImpl->WriteHID(outputMessage, (int)HID_REPORT_ID::I2C_READ_SET_ID);
		if (res.Failed())
			error_printf("WriteHID() FAILED for I2C address 0x%02x, register 0x%02x", inI2CAddress, inRegister);
		else
		{
			std::vector<uint8_t> inputMessage(I2C_BUFFER_HEADER_SIZE + MAX_COMM_READ_BUFFER_SIZE);
			res = mHIDImpl->ReadHID(inputMessage, (int)HID_REPORT_ID::I2C_READ_GET_ID);
			if (res.Failed())
				error_printf("ReadHID() FAILED for I2C address 0x%02x, register 0x%02x", inI2CAddress, inRegister);
			else
				memcpy(outData, inputMessage.data(), inLength);
		}
	}

	EPL_ASSERT_BREAK(res.Succeeded());
	return res;
}


EGAVResult ElgatoUVCDevice::WriteI2cData(uint8_t inI2CAddress, uint8_t inRegister, uint8_t* inData, uint8_t inLength)
{
	EGAVResult_CheckPointer(inData);
	EGAVResult_CheckPointer(mHIDImpl);

	const std::lock_guard<std::recursive_mutex> lock(mHIDMutex);

	EGAVResult res = EGAVResult::ErrUnknown;

	if (mNewDeviceType)
	{
		const uint8_t writeLen = 1 + inLength /* +1 for byte register address*/, reportLen = 4 + writeLen;
		std::vector<uint8_t> outputMessage{ reportLen, (uint8_t)REPORT_CASE_NEW::REPORT_IIC_WRITE, inI2CAddress, writeLen, inRegister };
		for (int i = 0; i < inLength; i++)
			outputMessage.push_back(inData[i]);
		EPL_ASSERT_BREAK(reportLen == outputMessage.size());

		res = mHIDImpl->WriteHID(outputMessage, (int)HID_REPORT_ID_NEW::I2C_WRITE);
	}
	else
	{
		std::vector<uint8_t> outputMessage{ inI2CAddress, inRegister, inLength };
		for (int i = 0; i < inLength; i++)
			outputMessage.push_back(inData[i]);
		res = mHIDImpl->WriteHID(outputMessage, (int)HID_REPORT_ID::I2C_WRITE_ID);
	}

	if (res.Failed())
		error_printf("WriteHID() FAILED for I2C address 0x%02d, register 0x%02d", inI2CAddress, inRegister);
	return res;
}

void ElgatoUVCDevice::SetHDRTonemappingEnabled(bool inValue)
{
	const std::lock_guard<std::recursive_mutex> lock(mHIDMutex);

	uint8_t buffer = inValue ? 1 : 0;
	WriteI2cData((uint8_t)I2CAddress::MCU, (uint8_t)MCU_I2C_REGISTER::XET_HDR_TONEMAPPING, &buffer, sizeof(buffer));
}

EGAVResult ElgatoUVCDevice::GetHDMIHDRStatusPacket(HDMI_GENERIC_INFOFRAME& outFrame)
{
	const std::lock_guard<std::recursive_mutex> lock(mHIDMutex);

	const size_t bufSize = mNewDeviceType ? 32 : 33;
	uint8_t* buffer = new uint8_t[bufSize];
	EGAVResult res = ReadI2cData((uint8_t)I2CAddress::MCU, (uint8_t)MCU_I2C_REGISTER::GET_HDR_PACKET, buffer, (uint8_t)bufSize);
	if (res.Succeeded())
	{
		size_t size = (bufSize < sizeof(outFrame)) ? bufSize : sizeof(outFrame);
		memcpy(&outFrame, mNewDeviceType ? buffer : buffer+1, size);

#if WORKAROUND_HD60_S_PLUS_PAYLOAD_SIZE
		// Workaround HD60 S+ firmware issue: invalid payload length (seen with HDR and SPD info frames)
		// Also with HD60 X FW 22.03.24 (MCU: 22.03.16)
		if (outFrame.header.bPayloadLength > HDMI_MAX_INFOFRAME_PAYLOAD)
		{
			if (HDMI_INFOFRAME_TYPE_DR == outFrame.header.bfType)
			{
				int diff = outFrame.header.bPayloadLength - sizeof(outFrame.plDR1);
				outFrame.header.bPayloadLength = sizeof(outFrame.plDR1);
				outFrame.bChecksum += diff;
			}
		}
#endif
	}
	delete [] buffer;
	return res;
}

EGAVResult ElgatoUVCDevice::IsVideoHDR(bool& outIsHDR)
{
	// Try to read HDR meta data
	HDMI_GENERIC_INFOFRAME frame{}, emptyFrame{};
	memset(&emptyFrame, 0, sizeof(emptyFrame));
	EGAVResult res = GetHDMIHDRStatusPacket(frame);
	if (res.Succeeded())
	{
		bool isInfoFrameValid = HDMI_IsInfoFrameValid(&frame);
		res = isInfoFrameValid ? EGAVResult::Ok : EGAVResult::ErrUnknown;
		if (isInfoFrameValid)
		{
			// Check type in header and EOTF flag in payload
			if (HDMI_INFOFRAME_TYPE_DR == frame.header.bfType && HDMI_DR_EOTF_SDRGAMMA != frame.plDR1.bfEOTF)
			{
				outIsHDR = true;
			}
			else if (HDMI_INFOFRAME_TYPE_DR == frame.header.bfType && HDMI_DR_EOTF_SDRGAMMA == frame.plDR1.bfEOTF)
			{
				outIsHDR = false; // we get here with HD60 X (22.03.24 (MCU: 22.03.16))
			}
			else if (0 /*HDMI_INFOFRAME_TYPE_RESERVED*/ == frame.header.bfType && (0 == memcmp(&frame, &emptyFrame, sizeof(emptyFrame))))
			{
				// all empty (seen with HD60 S+ when HDR is not active)
				outIsHDR = false;
			}
			else if (HDMI_INFOFRAME_TYPE_DR != frame.header.bfType)
			{
				warning_printf("HDMI Metadata: Wrong header type: %d", frame.header.bfType);
				res = EGAVResult::ErrNotFound;
			}
		}
		else
			warning_printf("HDMI Metadata: HDMI_IsInfoFrameValid() returned error (checksum)!");
	}
	else
		warning_printf("HDMI Metadata: GetHDMIHDRStatusPacket() failed!");
	return res;
}
