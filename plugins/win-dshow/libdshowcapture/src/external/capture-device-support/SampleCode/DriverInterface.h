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

//==============================================================================
/**
@file		DriverInterface.h

@brief		EGAVDeviceProperties class implementation. 
			Support for 4K60 Pro MK.2 and 4K60 S+
**/
//==============================================================================


#include <atlcomcli.h>
#include <strmif.h>

#include "HDMIInfoFramesAPI.h"

//! @brief Device properties for Elgato's non-UVC devices
class EGAVDeviceProperties
{
public:
	enum class DeviceType 
	{
		None = 0,
		GC4K60ProMK2, //!< 4K60 Pro MK.2: PCI\VEN_12AB& DEV_0710& SUBSYS_000E1CFA
		GC4K60SPlus   //!< 4K60 S+:       USB\VID_0FD9&PID_0068 or USB\VID_0FD9&PID_0075
	};

	//! @brief 
	//! @param inKsPropertySet Interface for driver property set. 
	//!                        Can be queried from the DirectShow filter via IBaseFilter::QueryInterface()
	//! @param inDeviceType 
	EGAVDeviceProperties(IKsPropertySet* inKsPropertySet, DeviceType inDeviceType);

	//! @brief 4K60 S+ only
	//! @param inHEVC 1 - HEVC, 0 - H.264
	HRESULT SetEncoderType(bool inHEVC);

	//! @brief 4K60 Pro MK.2 only
	//! @param inEnable 1 - enable tone mapping, 0 - disable HDR tonemapping
	HRESULT SetHDRTonemapping(bool inEnable);

	HRESULT IsVideoHDR(bool& outIsHDR);
	HRESULT GetHDMIHDRStatusPacket(uint8_t* outBuffer, int inBufferSize);

private:
	DeviceType					mDeviceType = DeviceType::None;
	GUID						mCustomPropertySetGUID = GUID_NULL;
	CComPtr<IKsPropertySet>		mICustomPropertySet;
};