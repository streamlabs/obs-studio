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

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

#include "ElgatoUVCDevice.h"


//==============================================================================
// # Constants
//==============================================================================

const EGAVDeviceID& selectedDeviceID = deviceIDHD60SPlus; // Select your device here


//==============================================================================
// # main()
//==============================================================================
int main()
{
	std::cout << "========================================" << std::endl;
	std::cout << " Sample:  HDR Tonemapping" << std::endl;
	std::cout << "========================================" << std::endl;
	std::cout << std::endl;

	std::shared_ptr<EGAVHIDInterface> hid = std::make_shared<EGAVHID>();
	EGAVResult res = hid->InitHIDInterface(selectedDeviceID);
	if (res.Failed())
	{
		std::cout << "InitHIDInterface() failed. Do you have the correct device connected?" << std::endl << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
	}
	else
	{
		ElgatoUVCDevice device(hid, IsNewDeviceType(selectedDeviceID));
		HDMI_GENERIC_INFOFRAME frame{};
		memset(&frame, 0, sizeof(frame));
		res = device.GetHDMIHDRStatusPacket(frame);
		if (res.Succeeded())
		{
			bool isHDR = false;
			res = device.IsVideoHDR(isHDR);
			std::cout << "Video is " << (isHDR ? "HDR" : "SDR") << std::endl;
			if (res.Succeeded() && isHDR)
			{
				std::cout << "Disable HDR tonemapping" << std::endl;
				device.SetHDRTonemappingEnabled(false);

#if 1 // TEST: TOGGLE TONEMAPPINING
				for (int i = 0; i < 2; i++)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(2000));
					std::cout << "Enable HDR tonemapping" << std::endl;
					device.SetHDRTonemappingEnabled(true);
					
					std::this_thread::sleep_for(std::chrono::milliseconds(2000));
					std::cout << "Disable HDR tonemapping" << std::endl;
					device.SetHDRTonemappingEnabled(false);
				}
#endif
			}
		}
		hid->DeinitHIDInterface();
	}

	return 0;
}
