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
@file		EGAVHIDImplementation.cpp

@brief		Windows implementation of EGAVHIDInterface
**/
//==============================================================================

// https://docs.microsoft.com/en-us/windows-hardware/drivers/hid/introduction-to-hid-concepts

#include "EGAVHIDImplementation.h"

#include <sstream> // for ostringstream

// Windows headers; for HID interface
#include <hidsdi.h>
#pragma comment(lib,"hid.lib")
#include <SetupAPI.h>
#include <atlstr.h>

#pragma comment(lib, "setupapi.lib")


#ifndef SAFE_CLOSE_HANDLE
	#define SAFE_CLOSE_HANDLE(_handle_)                           \
    {                                                             \
        if ((_handle_ != 0) && (_handle_ != INVALID_HANDLE_VALUE))\
        {                                                         \
            CloseHandle(_handle_);                                \
            _handle_= INVALID_HANDLE_VALUE;                       \
        }                                                         \
    }                        
#endif

std::string GetHIDDevicePath(int inIndex)
{
	std::string devicePath;

	GUID guid;
	HidD_GetHidGuid(&guid);

	HDEVINFO DeviceInfo = SetupDiGetClassDevs(&guid, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

	SP_DEVICE_INTERFACE_DATA DeviceInterface;
	DeviceInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, &guid, inIndex, &DeviceInterface))
	{
		SetupDiDestroyDeviceInfoList(DeviceInfo);
		return "";
	}

	unsigned long size = 0;
	SetupDiGetDeviceInterfaceDetail(DeviceInfo, &DeviceInterface, NULL, 0, &size, 0);

	PSP_INTERFACE_DEVICE_DETAIL_DATA pDeviceDetail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(size);
	if (pDeviceDetail)
	{
		pDeviceDetail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

		if (SetupDiGetDeviceInterfaceDetail(DeviceInfo, &DeviceInterface, pDeviceDetail, size, &size, NULL))
			devicePath = CT2A(pDeviceDetail->DevicePath);
		free(pDeviceDetail);
	}

	SetupDiDestroyDeviceInfoList(DeviceInfo);
	return devicePath;
}



std::shared_ptr<EGAVHIDInterface> CreateEGAVHIDInterface()
{
	return std::make_shared<EGAVHID>();
}


//==============================================================================
// ## Class EGAVHID
//==============================================================================

EGAVHID::EGAVHID()
	: mHIDCaps(std::make_unique<_HIDP_CAPS>())
{
}

EGAVResult EGAVHID::InitHIDInterface(const EGAVDeviceID& inDeviceID)
{
	EGAVResult res = EGAVResult::ErrNotFound;


	DWORD index = 0;
	std::string path;
	while ((path = GetHIDDevicePath(index++)) != "")
	{
		HANDLE hHidDevice = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hHidDevice == INVALID_HANDLE_VALUE)
			continue;

		bool isCorrectHidDevice = false;
		HIDD_ATTRIBUTES attr;
		if (HidD_GetAttributes(hHidDevice, &attr))
			isCorrectHidDevice = (attr.VendorID == inDeviceID.vendorID && attr.ProductID == inDeviceID.productID);

		if (isCorrectHidDevice)
		{
			mHIDHandle = hHidDevice;

			{
				PHIDP_PREPARSED_DATA p;
				HidD_GetPreparsedData(mHIDHandle, &p);
				HIDP_CAPS c;
				HidP_GetCaps(p, &c);

				*mHIDCaps = c;

				HidD_FreePreparsedData(p);
			}

			res = EGAVResult::Ok;
			break;
		}
		CloseHandle(hHidDevice);
	}

	return res;
}

EGAVResult EGAVHID::DeinitHIDInterface()
{
	SAFE_CLOSE_HANDLE(mHIDHandle);
	return EGAVResult::Ok;
}

//! This reads the report from the hardware
//! See Facemcam (Penna) sample code in https://elgato.atlassian.net/browse/EVH-493
EGAVResult EGAVHID::ReadHID(std::vector<uint8_t>& outMessage, int inReportID, int inReadBufferSize/* = 0*/)
{
	if (outMessage.size() >= mHIDCaps->InputReportByteLength)
		return EGAVResult::ErrInvalidParameter;

	std::vector<uint8_t> inputReport(mHIDCaps->InputReportByteLength);

	inputReport[0] = (uint8_t)inReportID;

	if (inReadBufferSize > 0)
		inputReport.resize(inReadBufferSize); // Required for Cam Link PD575 (EVH-1418)

	BOOL success = HidD_GetInputReport(mHIDHandle, &inputReport[0], (ULONG)inputReport.size());
	EGAVResult res = success ? EGAVResult::Ok : EGAVResult::ErrInvalidOperation;
	
                          // 121- ERROR_SEM_TIMEOUT
	                      // 31 - ERROR_GEN_FAILURE       - for invalid report ID
	if (FALSE == success) // 87 - ERROR_INVALID_PARAMETER - if (buffer size != caps.InputReportByteLength)
	{
		// error_printf("HidD_GetInputReport() for report ID %d FAILED with %d", inReportID, GetLastError());
	}
	else
	{
		outMessage.assign(inputReport.begin(), inputReport.end());
	}

	return res;
}

// this only prepends a zero byte (the report ID) to the message, pads it out to the size of an output
// report and sends it to the hardware

EGAVResult EGAVHID::WriteHID(const std::vector<uint8_t>& inMessage, int inReportID)
{
	if (!mHIDCaps)
		return EGAVResult::ErrInvalidState;

	if (inMessage.size() > mHIDCaps->OutputReportByteLength-1)
		return EGAVResult::ErrInvalidParameter;

	std::vector<uint8_t> outputReport(mHIDCaps->OutputReportByteLength, 0);
	
	outputReport[0] = (uint8_t)inReportID;
	memcpy(&outputReport[1], &inMessage[0], inMessage.size());
	
	// If the top-level collection includes report IDs, the caller must set the first byte of the ReportBuffer parameter to a non-zero report ID. 
	BOOL success = HidD_SetOutputReport(mHIDHandle, &outputReport[0], (ULONG)outputReport.size());
	EGAVResult res = success ? EGAVResult::Ok : EGAVResult::ErrInvalidOperation;
						  // 1167 - ERROR_DEVICE_NOT_CONNECTED
	if (FALSE == success) // 87 - ERROR_INVALID_PARAMETER - if (buffer size != c.OutputReportByteLength)
	{
		// error_printf("#### HID: HidD_SetOutputReport() FAILED with %d", GetLastError());
	}

	return res;
}