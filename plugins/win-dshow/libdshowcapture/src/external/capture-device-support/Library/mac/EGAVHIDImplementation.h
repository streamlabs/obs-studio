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
@file		EGAVHIDImplementation.h

@brief		macOS implementation of EGAVHIDInterface
**/
//==============================================================================

#pragma once

#include <memory>
#include <thread>

// macOS
#include <IOKit/hid/IOHIDManager.h>

#include "EGAVEngine/EGAVHID.h"

class HIDTransport;

class EGAVHID : public EGAVHIDInterface
{
public:
	EGAVHID();
	
	void DeviceAdded(IOHIDDeviceRef deviceRef);
	void DeviceRemoved(IOHIDDeviceRef deviceRef);

	//-----------------------------------------------------------------------------
	// ## EGAVHIDInterface implementation
	//-----------------------------------------------------------------------------
	virtual EGAVResult InitHIDInterface(const EGAVDeviceID& inDeviceID, EGAVUnitPtr inOwner, bool inIgnoreDevicePathCheck = false) override;
	virtual EGAVResult DeinitHIDInterface() override;

	//! @brief Reads a HID response message from the OS.
	//! @param outMessage will contain the resulting message. Its length will be adjusted automatically.
	virtual EGAVResult ReadHID(std::vector<uint8_t>& outMessage, int inReportID, int inReadBufferSize = 0) override;

	//! @brief Writes a HID report (with ID 0) containing inMessage to the OS.
	//! @param inMessage the report contents, not including the report ID. 
	virtual EGAVResult WriteHID(const std::vector<uint8_t>& inMessage, int inReportID) override;
	
private:
	int mLocationID = 0;
	int mInputReportSize = 0, mOutputReportSize = 0;

	std::atomic<IOHIDDeviceRef> mHIDDevice = nullptr;

	std::atomic<CFRunLoopRef> mRunLoop = nullptr;
	std::thread mWorker; //!< background worker for HID device discovery
	bool mWorkerCreated = false;
};
