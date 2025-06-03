/*
MIT License

Copyright (c) 2022 Corsair Memory, Inc.

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

//==============================================================================
/**
@file		DriverInterface.cpp

@brief		EGAVDeviceProperties class implementation
			Support for 4K60 Pro MK.2 and 4K60 S+
**/
//==============================================================================

#include "HDMIInfoFramesAPI.h"
#include "DriverInterface.h"

#ifndef EGAV_OVERRIDE_DEBUG_MACROS
	inline void dummy() {}

	#define warning_printf(...)   dummy()

	#ifndef HR_CHKRET_POINTER
		#define HR_CHKRET_POINTER(_ptr_)	if (!(_ptr_)) return E_POINTER;
	#endif
#endif


//! Property IDs for IKsPropertySet
enum class DriverProperty
{
	XET_ENCODER_VIDEO_FORMAT	= 400,	//!< 4K60 S+ encoder format. uint32_t parameter: 0 - H.264 and 1 - HEVC
	GET_HDMI_HDR_PACKET_00_15	= 720,	//!< HDMI HDR status packet - part 1 
	GET_HDMI_HDR_PACKET_16_31	= 721,	//!< HDMI HDR status packet - part 2 
	XET_HDMI_HDR_TO_SDR			= 722	//!< 4K60 Pro MK.2 Set HDR tonemapping. uint32_t parameter: 1 - on / 0 - off
};

static const int HDMI_PACKET_SIZE = 32;


EGAVDeviceProperties::EGAVDeviceProperties(IKsPropertySet* inKsPropertySet, DeviceType inDeviceType)
	: mICustomPropertySet(inKsPropertySet), mDeviceType(inDeviceType)
{
	switch (inDeviceType)
	{
	case EGAVDeviceProperties::DeviceType::GC4K60ProMK2:
		mCustomPropertySetGUID = { 0xD1E5209F, 0x68FD, 0x4529, 0xBE, 0xE0, 0x5E, 0x7A, 0x1F, 0x47, 0x92, 0x26 };
		break;
	case EGAVDeviceProperties::DeviceType::GC4K60SPlus:
		mCustomPropertySetGUID = { 0xD1E5209F, 0x68FD, 0x4529, 0xBE, 0xE0, 0x5E, 0x7A, 0x1F, 0x47, 0x92, 0x24 };
		break;
	default:
		break;
	}
}

HRESULT EGAVDeviceProperties::SetEncoderType(bool inHEVC)
{
	if (mDeviceType != DeviceType::GC4K60SPlus)
		return E_FAIL;

	HR_CHKRET_POINTER(mICustomPropertySet);

	uint32_t param = inHEVC ? 1 : 0;
	HRESULT hr = mICustomPropertySet->Set(mCustomPropertySetGUID, (DWORD)DriverProperty::XET_ENCODER_VIDEO_FORMAT, nullptr, 0, &param, sizeof(param));
	return hr;
}

HRESULT EGAVDeviceProperties::SetHDRTonemapping(bool inEnable)
{
	if (mDeviceType != DeviceType::GC4K60ProMK2)
		return E_FAIL;

	HR_CHKRET_POINTER(mICustomPropertySet);

	uint32_t param = inEnable ? 1 : 0;
	HRESULT hr = mICustomPropertySet->Set(mCustomPropertySetGUID, (DWORD)DriverProperty::XET_HDMI_HDR_TO_SDR, nullptr, 0, &param, sizeof(param));
	return hr;
}

HRESULT EGAVDeviceProperties::GetHDMIHDRStatusPacket(uint8_t *outBuffer, int inBufferSize)
{
	HR_CHKRET_POINTER(outBuffer);

	if (inBufferSize < HDMI_PACKET_SIZE)
		return E_INVALIDARG;

	HR_CHKRET_POINTER(mICustomPropertySet);

	DWORD dwRet = 0;
	HRESULT hr = mICustomPropertySet->Get(mCustomPropertySetGUID, (DWORD)DriverProperty::GET_HDMI_HDR_PACKET_00_15, nullptr, 0, &outBuffer[0], 16, &dwRet);
	if (SUCCEEDED(hr))
		hr = mICustomPropertySet->Get(mCustomPropertySetGUID, (DWORD)DriverProperty::GET_HDMI_HDR_PACKET_16_31, nullptr, 0, &outBuffer[16], 16, &dwRet);
	return hr ;
}

HRESULT EGAVDeviceProperties::IsVideoHDR(bool& outIsHDR)
{
	outIsHDR = false;

	// Try to read HDR meta data
	static const uint8_t emptyBuffer[HDMI_PACKET_SIZE] = { 0 };
	uint8_t buffer[HDMI_PACKET_SIZE] = { 0 };
	HRESULT hr = GetHDMIHDRStatusPacket(buffer, sizeof(buffer));
	if (SUCCEEDED(hr))
	{
		HDMI_GENERIC_INFOFRAME* frame = (HDMI_GENERIC_INFOFRAME*)(&buffer[0]);
		hr = (true == HDMI_IsInfoFrameValid(frame)) ? S_OK : E_FAIL;
		if (SUCCEEDED(hr))
		{
			// Check type in header and EOTF flag in payload
			if (HDMI_INFOFRAME_TYPE_DR == frame->header.bfType && HDMI_DR_EOTF_SDRGAMMA != frame->plDR1.bfEOTF)
			{
				outIsHDR = true;
			}
			else if (HDMI_INFOFRAME_TYPE_DR == frame->header.bfType && HDMI_DR_EOTF_SDRGAMMA == frame->plDR1.bfEOTF)
			{
				outIsHDR = false;
			}
			else if (0 /*HDMI_INFOFRAME_TYPE_RESERVED */ == frame->header.bfType && (0 == memcmp(buffer, emptyBuffer, sizeof(buffer))))
			{
				outIsHDR = false;
			}
			else if (HDMI_INFOFRAME_TYPE_DR != frame->header.bfType)
			{
				warning_printf("HDMI Metadata:  Wrong header type: %d", frame->header.bfType);
				hr = E_FAIL;
			}
		}
		else
			warning_printf("HDMI Metadata: HDMI_IsInfoFrameValid() returned error (checksum)!");
	}
	else
		warning_printf("HDMI Metadata: GetHDMIHDRStatusPacket() failed!");

	return hr;
}
