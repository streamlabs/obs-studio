HID API for Elgato UVC devices
==============================

The folder `Library` contains cross-platform code to access device-specific features of selected Elgato 
video products.

The CMake project in this folder builds a small console app for testing.
Modify `selectedDeviceID` in `SampleCode/main.cpp` to select the correct device type.

Supported platforms
-------------------
* Windows (10 or higher)
* macOS

Supported devices
-----------------
* HD60 S+
* HD60 X

Supported features
-----------------
* Switch on-device HDR tonemapping on/off
* Read HDMI HDR status packet (for HDR detection)

Limitations
-----------
The library was written for macOS and Windows.
However, the sample project was only built with Visual Studio 2019 and tested on Windows so far.

--------------------------------------------------------------------------------

Driver API for Elgato devices
=============================

For non-UVC devices device properties can be accessed via a custom driver property set (`IKsPropertySet`).

Sample code is provided, see `SampleCode/DriverInterface.h/.cpp`

Supported platforms
-------------------
* Windows (10 or higher)

Supported devices
-----------------
* 4K60 Pro MK.2
* 4K60 S+

Supported features
------------------
* Switch on-device HDR tonemapping on/off (4K60 Pro MK.2 only)
* Set video compression (4K60 S+ only)
* Read HDMI HDR status packet (for HDR detection)

#### 4K60 Pro MK.2    

To receive HDR the format on DirectShow filter pin must be set to P010.

#### 4K60 S+ 

The 4K60 S+ always produces compressed video output. 
For HDR the encoder format must be set to HEVC via the driver interface.


