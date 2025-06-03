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

@brief		macOS implementation of EGAVHIDInterface
**/
//==============================================================================

#include "EGAVHIDImplementation.h"
#include <IOKit/IOKitLib.h>
#include <thread>
#include <chrono>


//------------------------------------------------------------------------------
// PREPROCESSOR SWITCHES
//------------------------------------------------------------------------------

std::shared_ptr<EGAVHIDInterface> CreateEGAVHIDInterface()
{
	return std::make_shared<EGAVHID>();
}

//==============================================================================
// # HID Device Enumeration
//==============================================================================

static int LocationIDOfHIDDevice(IOHIDDeviceRef hidRef)
{
	int locationID = 0;

	// this block is just here to see if we can find the corresponding device

	CFTypeRef uniqueID = IOHIDDeviceGetProperty(hidRef, CFSTR(kIOHIDUniqueIDKey));
	if (CFGetTypeID(uniqueID) == CFNumberGetTypeID())
	{
		uint64_t uniqueID64 = 0;
		CFNumberGetValue((CFNumberRef)uniqueID, kCFNumberLongLongType, &uniqueID64);
		//NSLog(@"uniqueID %lld or 0x%16llx", uniqueID64, uniqueID64);

		CFMutableDictionaryRef matchingDict = IORegistryEntryIDMatching(uniqueID64);

		// next call consumes a reference to matchingDict
		io_service_t matchedService = IOServiceGetMatchingService(kIOMasterPortDefault, matchingDict);
		if (matchedService != 0)
		{
			CFNumberRef loc = (CFNumberRef)IORegistryEntrySearchCFProperty(matchedService, kIOServicePlane, CFSTR("locationID"), kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);
			if (loc != NULL)
			{
				CFNumberGetValue(loc, kCFNumberIntType, &locationID);
				CFRelease(loc);
			}
		}
	}

	return locationID;
}

static void HIDDeviceMatchingCallback(void* inContext,
									  IOReturn /*inResult*/,
									  void* /*inSender*/,
									  IOHIDDeviceRef inIOHIDDeviceRef)
{
	reinterpret_cast<EGAVHID*>(inContext)->DeviceAdded(inIOHIDDeviceRef);
}

static void HIDDeviceRemovalCallback(void* inContext,
									 IOReturn /*inResult*/,
									 void* /*inSender*/,
									 IOHIDDeviceRef inIOHIDDeviceRef)
{
	reinterpret_cast<EGAVHID*>(inContext)->DeviceRemoved(inIOHIDDeviceRef);
}




//==============================================================================
// # Class EGAVHID
//==============================================================================

EGAVHID::EGAVHID()
{
}

void EGAVHID::DeviceAdded(IOHIDDeviceRef deviceRef)
{
	if (mLocationID == 0) // we don't care about a specific locationID
	{
		mHIDDevice = deviceRef;
		info_printf("## DeviceAdded()");
	}
	else if (LocationIDOfHIDDevice(deviceRef) == mLocationID)
	{
		mHIDDevice = deviceRef;
		info_printf("## DeviceAdded(): Location ID %d", mLocationID);
	}
}

void EGAVHID::DeviceRemoved(IOHIDDeviceRef deviceRef)
{
	if (mHIDDevice == deviceRef)
	{
		info_printf("## DeviceRemoved()");
		mHIDDevice = 0;
	}
}


//==============================================================================
// ## HID interface
//==============================================================================

EGAVResult EGAVHID::InitHIDInterface(const EGAVDeviceID& inDeviceID, EGAVUnitPtr /*inOwner*/, bool /* inIgnoreDevicePathCheck = false */)
{
	dbgFunctionI();
	EGAVResult res = EGAVResult::Ok;

	mLocationID = inDeviceID.locationID;

	if (!mWorkerCreated) // program dies if thread is assigned when it isn't already null
	{
		mWorkerCreated = true;
		mWorker = std::thread([this, inDeviceID]
		{
			mRunLoop = CFRunLoopGetCurrent();

			IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);

			IOHIDManagerRegisterDeviceMatchingCallback(manager, HIDDeviceMatchingCallback, this);
			IOHIDManagerRegisterDeviceRemovalCallback(manager, HIDDeviceRemovalCallback, this);

			CFMutableDictionaryRef  matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

			CFNumberRef vendor = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &inDeviceID.vendorID);
			CFNumberRef product = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &inDeviceID.productID);

			CFDictionaryAddValue(matchingDict, CFSTR(kIOHIDVendorIDKey), vendor);
			CFDictionaryAddValue(matchingDict, CFSTR(kIOHIDProductIDKey), product);

			IOHIDManagerSetDeviceMatching(manager, matchingDict);

			IOHIDManagerScheduleWithRunLoop(manager, mRunLoop, kCFRunLoopDefaultMode);

			IOReturn ret = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
			EPL_ASSERT_BREAK(ret == kIOReturnSuccess);

			CFRunLoopRun();

			IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);

			CFRelease(manager);
		});
	}
	
	// Wait for device found
	int64_t startTimeMsec = EplTime_GetMonotonicMilliseconds();
	const int64_t kHidDiscoveryTimeoutMsec = 1500;
	while (!mHIDDevice && EplTime_GetMonotonicMilliseconds() - startTimeMsec < kHidDiscoveryTimeoutMsec)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	if (mHIDDevice)
	{
		// Query input report size //! @todo do this only once
		CFIndex         reportSize  = 0;
		CFTypeRef number = IOHIDDeviceGetProperty(mHIDDevice, CFSTR(kIOHIDMaxInputReportSizeKey));
		CFNumberGetValue((CFNumberRef)number, kCFNumberCFIndexType, &reportSize);
		mInputReportSize = (int)reportSize;

		number = IOHIDDeviceGetProperty(mHIDDevice, CFSTR(kIOHIDMaxOutputReportSizeKey));
		CFNumberGetValue((CFNumberRef)number, kCFNumberCFIndexType, &reportSize);
		mOutputReportSize = (int)reportSize;
	}
	
	return mHIDDevice ? EGAVResult::Ok : EGAVResult::ErrNotFound;
}

EGAVResult EGAVHID::DeinitHIDInterface()
{
	dbgFunctionI();
	mHIDDevice = nullptr;
	if (mRunLoop != nullptr)
	{
		CFRunLoopStop(mRunLoop);
		mWorker.join();
		mRunLoop = nullptr;
	}
	mWorkerCreated = false;
	return EGAVResult::Ok;
}

EGAVResult EGAVHID::ReadHID(std::vector<uint8_t>& outMessage, int inReportId, int inReadBufferSize /*= 0*/)
{
	EGAVResult_CheckPointer(mHIDDevice);
	
	EGAVResult res = EGAVResult::Ok;
	
	std::vector<uint8_t> report(mInputReportSize); // from the hardware, no zero prepended
	report[0] = inReportId;
	
	int usedSize = 0;

	{
		CFIndex bufferSize = inReadBufferSize > 0 ? inReadBufferSize : report.size();
		IOReturn err = IOHIDDeviceGetReport(mHIDDevice, kIOHIDReportTypeInput, inReportId,
											&report[0], &bufferSize);
		if (err == noErr)
			usedSize = (int)bufferSize;
		else error_printf("IOHIDDeviceGetReport() failed with IOReturn %d (0x%08X)", err, err);
		res = (err == noErr) ? EGAVResult::Ok : EGAVResult::ErrUnknown;
	}
	
	if (res.Succeeded())
	{
		outMessage.clear();

		// Facecam: calling code expects the report ID (0) in front of the report (Facecam)
		//! @todo check if this behavior is really necessary. Check on Windows also.
		if (inReportId == kHidDefaultReportID)
			outMessage.push_back(inReportId);

		for (int i = 0; i< usedSize; i++)
			outMessage.push_back(report[i]);
	}
	return res;
}

//! If device only has one report ID, it is zero (kHidDefaultReportID)
EGAVResult EGAVHID::WriteHID(const std::vector<uint8_t>& inMessage, int inReportID)
{
	// dbgFunctionI();

	EGAVResult_CheckPointer(mHIDDevice);

	std::vector<uint8_t> report;

	// From Device Class Definition for Human Interface Devices (HID) Version 1.11
	// If a device has multiple report structures, all data transfers start with a 1-byte identifier prefix that indicates which report structure
	// applies to the transfer. This allows the class driver to distinguish incoming pointer data from keyboard data by examining the transfer prefix.
	if (inReportID != kHidDefaultReportID)
		report.push_back(inReportID);

	for (auto m : inMessage)
		report.push_back(m);
	
	report.resize(mInputReportSize, 0); // pad report with zeros, ensure it is always the right length
	IOReturn err = IOHIDDeviceSetReport(mHIDDevice, kIOHIDReportTypeOutput, inReportID,
										&report[0], report.size());
	
	// 0xE00002D6 - kIOReturnTimeout
	// 0xE00002EB - kIOReturnAborted
	if (err != noErr)
		error_printf("IOHIDDeviceSetReport() failed with IOReturn %d (0x%08X)", err, err);
	
	EGAVResult res = (err == noErr) ? EGAVResult::Ok : EGAVResult::ErrUnknown;

	return res;
}
