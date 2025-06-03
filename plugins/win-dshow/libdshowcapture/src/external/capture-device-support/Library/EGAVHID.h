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
@file		EGAVHID.h

@brief		HID specific types and constants.
**/
//==============================================================================

#pragma once

#include <vector>

#include "EGAVResult.h"
#include "EGAVDevice.h" // required for EGAVDeviceID

const int kHidDefaultReportID = 0; //! Dummy report ID

//==============================================================================
// # Interfaces (HID)
//==============================================================================

//! @brief HID Interface for cross-platform support. (EVH-442)
class EGAVHIDInterface
{
public:
	virtual ~EGAVHIDInterface() { }

	virtual EGAVResult InitHIDInterface(const EGAVDeviceID& inDeviceID) = 0;
	virtual EGAVResult DeinitHIDInterface() = 0;

	//! @brief Reads a HID response message from the OS.
	//! @param outReceivedMessage will contain the resulting message. Its length will be adjusted automatically.
	//! @param report ID The reportID is always 0 (kHidDefaultReportID)  for Facecam (Penna)
	//! @param inReadBufferSize size of buffer passed to ID read routine. If 0 mHIDCaps->InputReportByteLength is used.
	virtual EGAVResult ReadHID(std::vector<uint8_t>& outMessage, int inReportID, int inReadBufferSize = 0) = 0;

	//! @brief Writes specified message.
	//!        The implementation should construct a HID report to send to the hardware
	//! @param message (NOT report!)
	//! @param report ID The reportID is always 0 (kHidDefaultReportID) for Facecam (Penna). The Penna-specific message tag is in the first byte of the message.
	virtual EGAVResult WriteHID(const std::vector<uint8_t>& inMessage, int inReportID) = 0;
};

//! @brief Platform specific factory method
std::shared_ptr<EGAVHIDInterface> CreateEGAVHIDInterface();
