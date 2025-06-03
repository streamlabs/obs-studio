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

#pragma once

#include <vector>
#include <mutex>
#include <thread>


//==============================================================================
// # Class EGAVDeviceID
//==============================================================================
enum class EGAVBusType { Unknown, USB, PCI };

struct EGAVDeviceID
{
	EGAVDeviceID() {}

	//! @brief EGAVDeviceID constructor
	//! @param inBusType    USB or PCIe
	//! @param inVendorID   USB vendor ID or PCIe subsystem vendor ID
	//! @param inProductID  USB product ID or PCIe subsystem device ID
	//! @param inLocationID Location ID (macOS only)
	EGAVDeviceID(EGAVBusType inBusType, uint16_t inVendorID, uint16_t inProductID, uint32_t inLocationID = 0) :
		busType(inBusType), vendorID(inVendorID), productID(inProductID), locationID(inLocationID)
	{
	}

	EGAVBusType	busType	                 = EGAVBusType::Unknown;
	uint16_t	vendorID                 = 0; //!< USB vendor ID or PCI subvendor ID
	uint16_t	productID                = 0; //!< USB product ID or PCI subdevice ID
	uint32_t	locationID               = 0; //!< USB location ID (macOS only)

	bool Equals(const EGAVDeviceID& inDeviceID, bool inIgnoreLocation) const
	{
		if (this->busType    != inDeviceID.busType)       return false;
		if (this->vendorID   != inDeviceID.vendorID)      return false;
		if (this->productID  != inDeviceID.productID)     return false;
		if (this->locationID != inDeviceID.locationID && !inIgnoreLocation)    return false;
		return true;
	}

	bool operator == (const EGAVDeviceID& inDeviceID) const { return  Equals(inDeviceID, false); }
	bool operator != (const EGAVDeviceID& inDeviceID) const { return !Equals(inDeviceID, false); }

	std::string toString();
};



