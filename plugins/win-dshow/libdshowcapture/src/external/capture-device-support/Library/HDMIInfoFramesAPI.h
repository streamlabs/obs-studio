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

#include <string>

#pragma pack(push, 1)


//====================================================================================
// # VIDEO IDENTIFICATION CODES (VIC)
//====================================================================================

// Video ID Code, see CEA-861-E chapter 4.1, table 4
// Video ID Code, see CEA-861-G chapter 4.1, table 3

typedef struct _HDMI_VIC_DESCRIPTOR
{
	uint8_t			bID;			//!< Video ID Code
	int				iWidth;			//!< width in pixels
	int				iHeight;		//!< height in pixels
	int				iFieldRate;		//!< field refresh rate in Hz
	bool			bInterlaced;	//!< interlaced
	short			iAspectX;		//!< picture aspect ratio H
	short			iAspectY;		//!< picture aspect ratio V
} 
HDMI_VIC_DESCRIPTOR;

// a field refresh value of 24Hz means either 24.00Hz or 23.98Hz
// a field refresh value of 30Hz means either 30.00Hz or 29.97Hz
// a field refresh value of 48Hz means either 48.00Hz or 47.95Hz
// a field refresh value of 60Hz means either 60.00Hz or 59.94Hz
// a field refresh value of 120Hz means either 120.00Hz or 119.88Hz
// a field refresh value of 240Hz means either 240.00Hz or 239.76Hz

#define HDMI_VIC_TABLE_SIZE		220

extern const HDMI_VIC_DESCRIPTOR g_HDMI_VIC_TABLE[HDMI_VIC_TABLE_SIZE];

//====================================================================================
// # INFO FRAME TYPES
//====================================================================================


// see CEA-861-E chapter 6, table 6
// see CEA-861-G chapter 6, table 5

#define HDMI_INFOFRAME_TYPE_RESERVED		0x00	// reserved

#define HDMI_INFOFRAME_TYPE_VS				0x01	// Vendor Specific
#define HDMI_INFOFRAME_TYPE_AVI				0x02	// Auxiliary Video Information
#define HDMI_INFOFRAME_TYPE_SPD				0x03	// Source Product Description
#define HDMI_INFOFRAME_TYPE_A				0x04	// Audio
#define HDMI_INFOFRAME_TYPE_MS				0x05	// MPEG Source
#define HDMI_INFOFRAME_TYPE_VBI				0x06	// NTSC VBI
#define HDMI_INFOFRAME_TYPE_DR				0x07	// Dynamic Range and Mastering

#define HDMI_INFOFRAME_TYPE_MIN				HDMI_INFOFRAME_TYPE_VS
#define HDMI_INFOFRAME_TYPE_MAX				HDMI_INFOFRAME_TYPE_DR


//====================================================================================
// # INFO FRAME HEADER
//====================================================================================

// see CEA-861-G Annex D.1

#define HDMI_MAX_INFOFRAME_SIZE		31		// 3 bytes header + 1 byte checksum + 27 bytes payload
#define HDMI_MAX_INFOFRAME_PAYLOAD  27		


// see CEA-861-E chapter 6

typedef struct _HDMI_INFOFRAMEHEADER
{
	uint8_t		bfType : 7;			// InfoFrame Type Code (see HDMI_INFOFRAME_TYPE_*)
	uint8_t		bfPacketType : 1;	// the HDMI Packet Type is 0x80+InfoframeType for HDMI InfoFrame Packets
	uint8_t		bfVersion : 7;		// InfoFrame Version Number, starting with 1
	uint8_t		bfChangeBit : 1;	// InfoFrame Change Bit, VS Infoframe only
	uint8_t		bPayloadLength;		// Size of InfoFrame payload, not including Type, Version, Length
}
HDMI_INFOFRAMEHEADER;


//====================================================================================
// # VENDOR SPECIFIC INFO FRAME
//====================================================================================

// see CEA-861-E chapter 6.1, table 7
// see CEA-861-G chapter 6.1, table 6 
// type code is 0x01, version is 0x01, size is vendor specific

typedef struct _HDMI_VS1_PAYLOAD
{
	uint8_t		IEEERegistrationID[3];		// IEEE OUI
	uint8_t		bVendorSpecificPayload[HDMI_MAX_INFOFRAME_SIZE - 3 - 3 - 1];	// 24 bytes
}
HDMI_VS1_PAYLOAD;


//------------------------------------------------------------------------------------


// see CEA-861-G chapter 6.1, table 7
// type code is 0x01, version is 0x02, size is vendor specific
// VS infoframe version 2 uses bit 7 of the version number as the ChangeBit 

typedef struct _HDMI_VS2_PAYLOAD
{
	uint8_t		IEEERegistrationID[3];		// IEEE OUI
	uint8_t		bVendorSpecificPayload[HDMI_MAX_INFOFRAME_SIZE - 3 - 3 - 1];	// 24 bytes
}
HDMI_VS2_PAYLOAD;


//====================================================================================
// # AUXILIARY VIDEO INFORMATION INFOFRAME
//====================================================================================

// Scan Information, see CEA-861-E chapter 6.4, table 10
#define HDMI_AVI_S_NODATA			0x00	// no data
#define HDMI_AVI_S_OVERSCAN			0x01	// composed for overscan
#define HDMI_AVI_S_UNDERSCAN		0x02	// composed for underscan

// Bar Data Present, see CEA-861-E chapter 6.4, table 10
#define HDMI_AVI_B_NODATA			0x00	// no data
#define HDMI_AVI_B_V				0x01	// vertical bar info present
#define HDMI_AVI_B_H				0x02	// horizontal bar info present
#define HDMI_AVI_B_VH				0x03	// vertical and horizontal bar info present

// Active Format Information Present, see CEA-861-E chapter 6.4, table 10
#define HDMI_AVI_A_NONE				0x00	// no information
#define HDMI_AVI_A_PRESENT			0x01	// information present

// RGB or YCbCr, see CEA-861-E chapter 6.4, table 10
// RGB or YCbCr, see CEA-861-G chapter 6.4, table 10
#define HDMI_AVI_Y_RGB				0x00	// RGB
#define HDMI_AVI_Y_YCBCR422			0x01	// YCbCr 4:2:2
#define HDMI_AVI_Y_YCBCR444			0x02	// YCbCr 4:4:4
#define HDMI_AVI_Y_YCBCR420			0x03	// YCbCr 4:2:0
#define HDMI_AVI_Y_IDO				0x07	// IDO-Defined

// Active Portion Aspect Ration, see CEA-861-E chapter 6.4, table 11
#define HDMI_AVI_R_SAME				0x08	// same as coded frame aspect ration
#define HDMI_AVI_R_4TO3				0x09	// 4:3 (center)
#define HDMI_AVI_R_16TO9			0x0A	// 16:9 (center)
#define HDMI_AVI_R_14TO9			0x0B	// 14:9 (center)

// Coded Frame Aspect Ration, see CEA-861-E chapter 6.4, table 11
#define HDMI_AVI_M_NODATA			0x00	// no data
#define HDMI_AVI_M_4TO3				0x01	// 4:3
#define HDMI_AVI_M_16TO9			0x02	// 16:9

// Colorimetry, see CEA-861-E chapter 6.4, table 11
#define HDMI_AVI_C_NODATA			0x00	// no data
#define HDMI_AVI_C_SMTPE170M		0x01	// SMTPE 170M
#define HDMI_AVI_C_ITUR709			0x02	// ITU-R 709
#define HDMI_AVI_C_EXTENDED			0x03	// extended colorimetry information valid

// Non-Uniform Picture Scaling, see CEA-861-E chapter 6.4, table 13
#define HDMI_AVI_SC_NO				0x00	// no known scaling
#define HDMI_AVI_SC_H				0x01	// picture has been scaled horizontally
#define HDMI_AVI_SC_V				0x02	// picture has been scaled vertically
#define HDMI_AVI_SC_HV				0x03	// picture has been scaled horizontally and vertically

// RGB Quantization Range, see CEA-861-E chapter 6.4, table 13
#define HDMI_AVI_Q_DEFAULT			0x00	// default, depends on video format
#define HDMI_AVI_Q_LIMITED			0x01	// limited range
#define HDMI_AVI_Q_FULL				0x02	// full range

// Extended Colorimetry, see CEA-861-E chapter 6.4, table 13
// Extended Colorimetry, see CEA-861-G chapter 6.4, table 13
#define HDMI_AVI_EC_XVYCC601		0x00	// xvYCC 601
#define HDMI_AVI_EC_XVYCC709		0x01	// xvYCC 709
#define HDMI_AVI_EC_SYCC601			0x02	// sYCC 601
#define HDMI_AVI_EC_ADOBEYCC601		0x03	// Adobe YCC 601
#define HDMI_AVI_EC_ADOBERGB		0x04	// Adobe RGB
#define HDMI_AVI_EC_BT2020C			0x05	// ITU BT2020 YcCbcCrc
#define HDMI_AVI_EC_BT2020			0x06	// ITU BT2020 RGB or YCbCr
#define HDMI_AVI_EC_EXTENDED		0x07	// extended information, see HDMI_AVI_ACE_*

// IT Content, see CEA-861-E chapter 6.4, table 13
#define HDMI_AVI_ITC_NODATA			0x00	// no data
#define HDMI_AVI_ITC_VALID			0x01	// IT Content, CN is valid

// Pixel Repetition Factor, see CEA-861-E chapter 6.4, table 15
#define HDMI_AVI_PR_NONE			0x00	// pixels are not repeated, i.e. only sent once in total
#define HDMI_AVI_PR_1				0x01	// pixels are repeated once, i.e. sent twice in total
#define HDMI_AVI_PR_2				0x02	// pixels are repeated twice, i.e. sent three times in total
#define HDMI_AVI_PR_3				0x03	// pixels are repeated three times, i.e. sent 4 times in total
#define HDMI_AVI_PR_4				0x04	// pixels are repeated 4 times, i.e. sent 5 times in total
#define HDMI_AVI_PR_5				0x05	// pixels are repeated 5 times, i.e. sent 6 times in total
#define HDMI_AVI_PR_6				0x06	// pixels are repeated 6 times, i.e. sent 7 times in total
#define HDMI_AVI_PR_7				0x07	// pixels are repeated 7 times, i.e. sent 8 times in total
#define HDMI_AVI_PR_8				0x08	// pixels are repeated 8 times, i.e. sent 9 times in total
#define HDMI_AVI_PR_9				0x09	// pixels are repeated 9 times, i.e. sent 10 times in total

// IT Content Type, see CEA-861-E chapter 6.4, table 16
#define HDMI_AVI_CN_GRAPHICS		0x00	// graphics
#define HDMI_AVI_CN_PHOTO			0x01	// photo
#define HDMI_AVI_CN_CINEMA			0x02	// cinema
#define HDMI_AVI_CN_GAME			0x03	// game

// YCC Quantization Range, see CEA-861-E chapter 6.4, table 17
#define HDMI_AVI_YQ_LIMITED			0x00	// limited range
#define HDMI_AVI_YQ_FULL			0x01	// full range

// Additional Colorimetry Extension, see CEA-861-G chapter 6.4, table 25
#define HDMI_AVI_ACE_DCIP3D65		0x00	// DCI-P3 R'G'B' (D65)
#define HDMI_AVI_ACE_DCIP3TH		0x01	// DCI-P3 R'G'B' (Theater)


//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.3, table 8
// type code is 0x02, version is 0x01, size is 13 
// ATTENTION: this AVI version is obsolete and should not be used!

typedef struct _HDMI_AVI1_PAYLOAD
{
	// data byte 1, see CEA-861-E chapter 6.4, table 10
	uint8_t		bfScanInformation : 2;					// S0, S1
	uint8_t		bfBarDataPresent : 2;					// B0, B1
	uint8_t		bfActiveFormatInformationPresent : 1;	// A0
	uint8_t		bfRGBorYCbCr : 2;						// Y0, Y1
	uint8_t		bfFutureUse1 : 1;						// reserved, zero

	// data byte 2, see CEA-861-E chapter 6.4, table 11
	uint8_t		bfActivePortionAspectRatio : 4;			// R0, R1, R2, R3
	uint8_t		bfCodedFrameAspectRatio : 2;			// M0, M1
	uint8_t		bfColorimetry : 2;						// C0, C1

	// data byte 3, see CEA-861-E chapter 6.4, table 13
	uint8_t		bfNonUniformPictureScaling : 2;			// SC0, SC1
	uint8_t		bfFutureUse3 : 6;						// reserved, zero

	uint8_t		bfFutureUse4 = 0;						// reserved, zero
	uint8_t		bfFutureUse5 = 0;						// reserved, zero

	uint16_t	wLineNumberOfEndOfTopBar;				// ETB
	uint16_t	wLineNumberOfStartOfBottomBar;			// SBB
	uint16_t	wPixelNumberOfEndOfLeftBar;				// ELB
	uint16_t	wPixelNumberOfStartOfRightBar;			// SRB
}
HDMI_AVI1_PAYLOAD;


//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.4, table 9
// see CEA-861-E chapter 6.4, table 8
// type code is 0x02, version is 0x02, size is 13 

typedef struct _HDMI_AVI2_PAYLOAD
{
	// data byte 1, see CEA-861-E chapter 6.4, table 10
	uint8_t		bfScanInformation : 2;					// S0, S1
	uint8_t		bfBarDataPresent : 2;					// B0, B1
	uint8_t		bfActiveFormatInformationPresent : 1;	// A0
	uint8_t		bfRGBorYCbCr : 2;						// Y0, Y1
	uint8_t		bfFutureUse1 : 1;						// reserved, zero

	// data byte 2, see CEA-861-E chapter 6.4, table 11
	uint8_t		bfActivePortionAspectRatio : 4;			// R0, R1, R2, R3
	uint8_t		bfCodedFrameAspectRatio : 2;			// M0, M1
	uint8_t		bfColorimetry : 2;						// C0, C1

	// data byte 3, see CEA-861-E chapter 6.4, table 13
	uint8_t		bfNonUniformPictureScaling : 2;			// SC0, SC1
	uint8_t		bfRGBQuantizationRange : 2;				// Q0, Q1
	uint8_t		bfExtendedColorimetry : 3;				// EC0, EC1, EC2
	uint8_t		bfITContent : 1;						// ITC

	// data byte 4, see CEA-861-E chapter 4.1, table 4
	uint8_t		bfVIC : 7;								// VIC
	uint8_t		bfFutureUse4 : 1;						// reserved, zero

	// data byte 5, see CEA-861-E chapter 6.4, tables 15, 16, 17
	uint8_t		bfPixelRepetitionFactor : 4;			// PR0, PR1, PR2, PR3
	uint8_t		bfITContentType : 2;					// CN0, CN1
	uint8_t		bfYCCQuantizationRange : 2;				// YQ0, YQ1

	uint16_t	wLineNumberOfEndOfTopBar;				// ETB
	uint16_t	wLineNumberOfStartOfBottomBar;			// SBB
	uint16_t	wPixelNumberOfEndOfLeftBar;				// ELB
	uint16_t	wPixelNumberOfStartOfRightBar;			// SRB
}
HDMI_AVI2_PAYLOAD;


//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.4, table 8
// type code is 0x02, version is 0x03, size is 13 

typedef struct _HDMI_AVI3_PAYLOAD 
{
	// data byte 1, see CEA-861-G chapter 6.4, table 10
	uint8_t		bfScanInformation : 2;					// S0, S1
	uint8_t		bfBarDataPresent : 2;					// B0, B1
	uint8_t		bfActiveFormatInformationPresent : 1;	// A0
	uint8_t		bfRGBorYCbCr : 3;						// Y0, Y1, Y2

	// data byte 2, see CEA-861-E chapter 6.4, table 11
	uint8_t		bfActivePortionAspectRatio : 4;			// R0, R1, R2, R3
	uint8_t		bfCodedFrameAspectRatio : 2;			// M0, M1
	uint8_t		bfColorimetry : 2;						// C0, C1

	// data byte 3, see CEA-861-E chapter 6.4, table 13
	uint8_t		bfNonUniformPictureScaling : 2;			// SC0, SC1
	uint8_t		bfRGBQuantizationRange : 2;				// Q0, Q1
	uint8_t		bfExtendedColorimetry : 3;				// EC0, EC1, EC2
	uint8_t		bfITContent : 1;						// ITC

	// data byte 4, see CEA-861-E chapter 4.1, table 4
	uint8_t		bVIC;									// VIC

	// data byte 5, see CEA-861-E chapter 6.4, tables 15, 16, 17
	uint8_t		bfPixelRepetitionFactor : 4;			// PR0, PR1, PR2, PR3
	uint8_t		bfITContentType : 2;					// CN0, CN1
	uint8_t		bfYCCQuantizationRange : 2;				// YQ0, YQ1

	uint16_t	wLineNumberOfEndOfTopBar;				// ETB
	uint16_t	wLineNumberOfStartOfBottomBar;			// SBB
	uint16_t	wPixelNumberOfEndOfLeftBar;				// ELB
	uint16_t	wPixelNumberOfStartOfRightBar;			// SRB
}
HDMI_AVI3_PAYLOAD;


//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.4, table 9
// type code is 0x02, version is 0x04, size is 14

typedef struct _HDMI_AVI4_PAYLOAD 
{
	// data byte 1, see CEA-861-G chapter 6.4, table 10
	uint8_t		bfScanInformation : 2;					// S0, S1
	uint8_t		bfBarDataPresent : 2;					// B0, B1
	uint8_t		bfActiveFormatInformationPresent : 1;	// A0
	uint8_t		bfRGBorYCbCr : 3;						// Y0, Y1, Y2

	// data byte 2, see CEA-861-E chapter 6.4, table 11
	uint8_t		bfActivePortionAspectRatio : 4;			// R0, R1, R2, R3
	uint8_t		bfCodedFrameAspectRatio : 2;			// M0, M1
	uint8_t		bfColorimetry : 2;						// C0, C1

	// data byte 3, see CEA-861-E chapter 6.4, table 13
	uint8_t		bfNonUniformPictureScaling : 2;			// SC0, SC1
	uint8_t		bfRGBQuantizationRange : 2;				// Q0, Q1
	uint8_t		bfExtendedColorimetry : 3;				// EC0, EC1, EC2
	uint8_t		bfITContent : 1;						// ITC

	// data byte 4, see CEA-861-E chapter 4.1, table 4
	uint8_t		bVIC;									// VIC

	// data byte 5, see CEA-861-E chapter 6.4, tables 15, 16, 17
	uint8_t		bfPixelRepetitionFactor : 4;			// PR0, PR1, PR2, PR3
	uint8_t		bfITContentType : 2;					// CN0, CN1
	uint8_t		bfYCCQuantizationRange : 2;				// YQ0, YQ1

	uint16_t	wLineNumberOfEndOfTopBar;				// ETB
	uint16_t	wLineNumberOfStartOfBottomBar;			// SBB
	uint16_t	wPixelNumberOfEndOfLeftBar;				// ELB
	uint16_t	wPixelNumberOfStartOfRightBar;			// SRB

	// data byte 14, see CEA-861-G chapter 6.4, tables 15, 16, 17
	uint8_t		bfReserved14 : 4;						// reserved, zero
	uint8_t		bfAdditionalColorimetry : 4;			// ACE
}
HDMI_AVI4_PAYLOAD;


//------------------------------------------------------------------------------------


#define HDMI_ERROR				-1

#define HDMI_UNKNOWN			0

#define HDMI_FORMAT_RGB			1		// RGB
#define HDMI_FORMAT_YCBCR420	2		// YCbCr 4:2:0
#define HDMI_FORMAT_YCBCR422	3		// YCbCr 4:2:2
#define HDMI_FORMAT_YCBCR444	4		// YCbCr 4:4:4

#define HDMI_COLOR_ADOBERGB		1
#define HDMI_COLOR_BT2020		2
#define HDMI_COLOR_DCIP3D65		3
#define HDMI_COLOR_DCIP3TH		4
#define HDMI_COLOR_SMPTE170M	5	
#define HDMI_COLOR_BT709		6
#define HDMI_COLOR_XVYCC601		7
#define HDMI_COLOR_XVYCC709		8
#define HDMI_COLOR_SYCC601		9
#define HDMI_COLOR_ADOBEYCC601	10
#define HDMI_COLOR_BT2020C		11


//====================================================================================
// # SOURCE PRODUCT DESCRIPTION INFOFRAME  (SPD)
//====================================================================================


// Source Information, see CEA-861-E chapter 6.5, table 22
#define HDMI_SPD_SI_UNKNOWN			0x00
#define HDMI_SPD_SI_STB				0x01
#define HDMI_SPD_SI_DVD				0x02
#define HDMI_SPD_SI_DVHS			0x03
#define HDMI_SPD_SI_DVR				0x04
#define HDMI_SPD_SI_DVC				0x05
#define HDMI_SPD_SI_DSC				0x06
#define HDMI_SPD_SI_VCD				0x07
#define HDMI_SPD_SI_GAME			0x08
#define HDMI_SPD_SI_PC				0x09
#define HDMI_SPD_SI_BD				0x0A
#define HDMI_SPD_SI_SACD			0x0B
#define HDMI_SPD_SI_HDDVD			0x0C
#define HDMI_SPD_SI_PMP				0x0D

const char* HDMI_SPD_ToString(uint8_t inByte);

//! @brief Maps abbreviated names to user-friendly names (e.g. MSFT --> Microsoft)
//! @param inManufacturer manufacturer string as contained in HDMI info packet
std::string HDMI_SPD_MapManufacturerString(const std::string& inManufacturer);

//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.5, table 21
// type code is 0x03, version is 1, size is 25

typedef struct _HDMI_SPD1_PAYLOAD 
{
	uint8_t		bVendorName[8];
	uint8_t		bProductDescription[16];
	uint8_t		bSourceInformation;
}
HDMI_SPD1_PAYLOAD;


//====================================================================================
// AUDIO INFOFRAME 
//====================================================================================


// Audio Channel Count, see CEA-861-E chapter 6.6.1, table 24
#define HDMI_A_CC_STREAM			0x00	// see stream header

// Audio Coding Type, see CEA-861-E chapter 6.6.1, table 24
#define HDMI_A_CT_STREAM			0x00	// see stream header
#define HDMI_A_CT_PCM				0x01	// PCM
#define HDMI_A_CT_AC3				0x02	// AC-3
#define HDMI_A_CT_MPEG1				0x03	// MPEG-1
#define HDMI_A_CT_MP3				0x04	// MP3
#define HDMI_A_CT_MPEG2				0x05	// MPEG-2
#define HDMI_A_CT_AACLC				0x06	// AAC-LC
#define HDMI_A_CT_DTS				0x07	// DTS
#define HDMI_A_CT_ATRAC				0x08	// ATRAC
#define HDMI_A_CT_DSD				0x09	// DSD
#define HDMI_A_CT_EAC3				0x0A	// E-AC-3
#define HDMI_A_CT_DTSHD				0x0B	// DTS-HD
#define HDMI_A_CT_MLP				0x0C	// MLP
#define HDMI_A_CT_DST				0x0D	// DST
#define HDMI_A_CT_WMAPRO			0x0E	// WMA Pro
#define HDMI_A_CT_CXT				0x0F	// see Audio Coding Extension Type (CXT)

// Sample Size, see CEA-861-E chapter 6.6.1, table 25
#define HDMI_A_SS_STREAM			0x00	// see stream header	
#define HDMI_A_SS_16BIT				0x01	// 16bit
#define HDMI_A_SS_20BIT				0x02	// 20bit
#define HDMI_A_SS_24BIT				0x03	// 24bit

// Sample Frequency, see CEA-861-E chapter 6.6.1, table 25
#define HDMI_A_SF_STREAM			0x00	// see stream header
#define HDMI_A_SF_32000				0x01	// 32kHz
#define HDMI_A_SF_44100				0x02	// 44.1kHz
#define HDMI_A_SF_48000				0x03	// 48kHz
#define HDMI_A_SF_88200				0x04	// 88.2kHz
#define HDMI_A_SF_96000				0x05	// 96kHz
#define HDMI_A_SF_176400			0x06	// 176.4kHz
#define HDMI_A_SF_192000			0x07	// 192kHz

// Audio Coding Extension Type, see CEA-861-E chapter 6.6.1, table 26
#define HDMI_A_CXT_CT				0x00	// see Audio Coding Type (CT)
#define HDMI_A_CXT_HEAAC			0x01	// HE-AAC
#define HDMI_A_CXT_HEAAC2			0x02	// HE-AAC v2
#define HDMI_A_CXT_MPEGSURROUND		0x03	// MPEG Surround

// Down-mix Inhibit, see CEA-861-E chapter 6.6.2, table 30
#define HDMI_A_DM_PERMITTED			0x00	// down mix permitted
#define HDMI_A_DM_PROHIBITED		0x01	// down mix prohibited

// LFE Playback Level, see CEA-861-E chapter 6.6.2, table 31
#define HDMI_A_LFEPBL_UNKNOWN		0x00	// unknown
#define HDMI_A_LFEPBL_0DB			0x01	// 0 dB
#define HDMI_A_LFEPBL_10DB			0x02	// +10 dB


//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.6, table 23
// type code is 0x04, version is 1, size is 10

typedef struct _HDMI_A1_PAYLOAD 
{
	// data byte 1, see CEA-861-E chapter 6.6.1, table 24
	uint8_t		bfChannelCount : 3;						// CC0, CC1, CC2 (channel count - 1)
	uint8_t		bfReserved1 : 1;						// reserved, zero
	uint8_t		bfAudioCodingType : 4;					// CT0, CT1, CT2, CT3

	// data byte 2, see CEA-861-E chapter 6.6.1, table 25
	uint8_t		bfSampleSize : 2;						// SS0, SS1
	uint8_t		bfSampleFrequency : 3;					// SF0, SF1, SF2
	uint8_t		bfReserved2 : 3;						// reserved, zero

	// data byte 3, see CEA-861-E chapter 6.6.1, table 26
	uint8_t		bfAudioCodingExtensionType : 5;			// CXT0, CXT1, CXT2, CXT3, CXT4
	uint8_t		bfReserved3 : 3;						// reserved, zero

	// data byte 4, see CEA-861-E chapter 6.6.2, table 28
	uint8_t		bChannelAllocation;						// CA (channel to speaker)

	// data byte 5, see CEA-861-E chapter 6.6.2, tables 29, 30, 31
	uint8_t		bfLFEPlaybackLevel : 2;					// LFEPBL0, LFEPBL1
	uint8_t		bfReserved5 : 1;						// reserved, zero
	uint8_t		bfLevelShiftValue : 4;					// LSV0, LSV1 (dB)
	uint8_t		bfDownMixInhibitFlag : 1;				// DM_INH

	uint8_t		bReserved6 = 0;							// reserved, or Speaker Mask, or Channel Index
	uint8_t		bReserved7 = 0;							// reserved, or Speaker Mask, or Channel Index
	uint8_t		bReserved8 = 0;							// reserved, or Speaker Mask, or Channel Index
	uint8_t		bReserved9 = 0;							// reserved, or Speaker Mask, or Channel Index
	uint8_t		bReserved10 = 0;						// reserved, zero
}
HDMI_A1_PAYLOAD;


//====================================================================================
// MPEG SOURCE INFOFRAME 
//====================================================================================


// MPEG Frame, see CEA-861-E chapter 6.7, table 33
#define HDMI_MS_MF_UNKNOWN			0x00	// unknown
#define HDMI_MS_MF_I      			0x01	// I-Frame
#define HDMI_MS_MF_P      			0x02	// P-Frame
#define HDMI_MS_MF_B      			0x03	// B-Frame

// Field Repeat, see CEA-861-E chapter 6.7, table 33
#define HDMI_MS_FR_NEW  			0x00	// new field
#define HDMI_MS_FR_REPEATED			0x01	// repeated field


//------------------------------------------------------------------------------------


// see CEA-861-E chapter 6.7, table 32
// type code is 0x05, version is 1, size is 10
// ATTENTION: it is recommended not to use this info frame

typedef struct _HDMI_MS1_PAYLOAD 
{
	uint32_t	dwMPEGBitRate;							// MPEG bit rate in Hz

	// data byte 5, see CEA-861-E chapter 6.7, table 33
	uint8_t		bfMPEGFrame : 2;						// MF0, MF1
	uint8_t		bfReserved5a : 2;						// reserved, zero
	uint8_t		bfFieldRepeat : 1;						// FR0
	uint8_t		bfReserved5b : 3;						// reserved, zero

	uint8_t		bReserved6 = 0;							// reserved
	uint8_t		bReserved7 = 0;							// reserved
	uint8_t		bReserved8 = 0;							// reserved
	uint8_t		bReserved9 = 0;							// reserved
	uint8_t		bReserved10 = 0;						// reserved
}
HDMI_MS1_PAYLOAD;


//====================================================================================
// NTSC VBI INFOFRAME 
//====================================================================================


// see CEA-861-E chapter 6.8, table 34
// type code is 0x06, version is 1, size depends

typedef struct _HDMI_VBI1_PAYLOAD 
{
	uint8_t		bPESDataField[HDMI_MAX_INFOFRAME_SIZE - 3 - 1];		// PES data field, limited to max 27 bytes
}
HDMI_VBI1_PAYLOAD;


//====================================================================================
// # DYNAMIC RANGE AND MASTERING INFOFRAME 
//====================================================================================

/*
Example for valid DR info frame:

87	(type)					| Header
01	(version)				| 
1A	(length)				|
8D	(checksum)
02 00 FA 00 AE 02 85 00		| Payload
29 00 A3 02 5C 01 40 01		|
51 01 DB 05 00 00 DB 05		|
1F 03						|
*/

//------------------------------------------------------------------------------------


// EOTF, see CEA-861.3-A chapter 3.2, table 3
#define HDMI_DR_EOTF_SDRGAMMA		0x00	// traditional gamma, SDR	
#define HDMI_DR_EOTF_HDRGAMMA		0x01	// traditional gamma, HDR
#define HDMI_DR_EOTF_ST2084			0x02	// ST2084 PQ
#define HDMI_DR_EOTF_HLG			0x03	// BT2100 HLG

// Metadata, see CEA-861.3-A chapter 3.2, table 4
#define HDMI_DR_MD_STATIC			0x00	// static metadata type 1


//------------------------------------------------------------------------------------


// used for static metadata, see CEA-861.3-A chapter 3.2.1
typedef struct _HDMI_XY
{
	uint16_t	X;		// encoded in units of 0.00002
	uint16_t	Y;		// encoded in units of 0.00002
}
HDMI_XY;


//------------------------------------------------------------------------------------

//! HDR Meta Data
//! type code is 0x07, version is 1, size depends (30 for static metadata type 1)
//! @sa CEA-861.3-A chapter 3.2, table 2

typedef struct _HDMI_DR1_PAYLOAD 
{
	// data byte 1, see CEA-861.3-A chapter 3.2, table 3
	uint8_t		bfEOTF : 3;								// EOTF
	uint8_t		bfReserved1 : 5;						// reserved

	// data byte 2, see CEA-861.3-A chapter 3.2, table 4
	uint8_t		bfMetadataID : 3;						// static metadata descriptor ID
	uint8_t		bfReserved2 : 5;						// reserved

	// data bytes 3-22, for static metadata type 1, see CEA-861.3-A chapter 3.2.1, table 5
	HDMI_XY		xyDisplayPrimaries[3];					// chromaticity of red or green or blue (ST2086)
	HDMI_XY		xyWhitePoint;							// white point (ST2086)
	uint16_t	wMaxDisplayLuminance;					// maximum display mastering luminance (ST2086), nit
	uint16_t	wMinDisplayLuminance;					// minimum display mastering luminance (ST2086), 0.0001 nit
	uint16_t	wMaxCLL;								// maximum content light level, nit
	uint16_t	wMaxFALL;								// maximum frame-average light level, nit
}
HDMI_DR1_PAYLOAD;


//====================================================================================
// GENERIC INFOFRAME TYPE
//====================================================================================


typedef struct _HDMI_GENERIC_INFOFRAME
{
	HDMI_INFOFRAMEHEADER		header;		// type, version, length
	uint8_t						bChecksum;	// the sum of all bytes in the info frame must be zero

	union
	{
		// generic byte array, to address the InfoFrame by index
		uint8_t					bPayload[HDMI_MAX_INFOFRAME_SIZE - 3 - 1]  = { 0 };

		// specific infoframes
		HDMI_VS1_PAYLOAD		plVS1;
		HDMI_VS2_PAYLOAD		plVS2;
		HDMI_AVI1_PAYLOAD		plAVI1;
		HDMI_AVI2_PAYLOAD		plAVI2;
		HDMI_AVI3_PAYLOAD		plAVI3;
		HDMI_AVI4_PAYLOAD		plAVI4;
		HDMI_SPD1_PAYLOAD		plSPD1;
		HDMI_A1_PAYLOAD			plA1;
		HDMI_MS1_PAYLOAD		plMS1;
		HDMI_VBI1_PAYLOAD		plVBI1;
		HDMI_DR1_PAYLOAD		plDR1;
	};
}
HDMI_GENERIC_INFOFRAME;


#pragma pack(pop)


//====================================================================================
// # FUNCTIONS
//====================================================================================

// Verifies the checksum of the HDMI Info Frame
inline bool HDMI_IsInfoFrameValid(const _HDMI_GENERIC_INFOFRAME* pInfoFrame)
{
	if (pInfoFrame == NULL) return false;

	unsigned char* data = (unsigned char*)pInfoFrame;
	int size = sizeof(HDMI_INFOFRAMEHEADER) + 1 + pInfoFrame->header.bPayloadLength;

	unsigned char checksum = 0;
	while (size-- != 0) checksum += *data++;

	return (checksum == 0);
}



