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
@file		EGAVResult.h

@brief		Definition of error and success codes
**/
//==============================================================================

#pragma once


//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <string>
#if _UP_WINDOWS 
	// FMB NOTE: For some strange reason <WinSock2.h> must be before <Windows.h>. 
	// This is only necessary because contents of <WinSock2.h> are required in other code files.
	// However, if <WinSock2.h> is include somewhere it must be guaranteded that <Windows.h> was never include before.
	#include <WinSock2.h>	// for sockaddr_in
	#include <Windows.h>
#endif

//#include "EGAVFeatures.h" // for EGAV_API


#define EVH_AVOID_UNNEEDED_COPY_CONSTRUCTORS 			(!_UP_WINDOWS)	//!< 0 - explicit copy constructors: required for .NET frontends connected via SWIG e.g. 4KCU and EVH Test App
																		//!< 1 - to avoid "warning: definition of implicit copy constructor for different classes e.g.
																		//! 'EGAVAudioSampleType' is deprecated because it has a user-declared copy assignment operator [-Wdeprecated-copy]"





//==============================================================================
// EGAVResultCustomType
//==============================================================================

enum class EGAVResultCustomType
{
	None,
	Hresult,					//!< HRESULT (Windows)
	WinError,					//!< System error code (Windows)
	MainConcept,				//!< MainConcept error codes (BS_OK etc.)
	Device,						//!< Device Error Codes
    Mac                         //!< Errors from macOS
};

enum class EGAVResultCustomTypeDevice {
	None,
    SpeedInsufficient,
    ResultUnexpected,
};

//==============================================================================
// EGAVResult
//==============================================================================
typedef int EGAVResultCode;

//! Error description
class /*EGAV_API*/ EGAVResult
{
public:

	//------------------------------------------------------------------------------
	// Constants
	//------------------------------------------------------------------------------

	// EXTEND_EGAVResultCode: add new error codes here and in GetResultCodeString function in cpp file
	static const EGAVResultCode ErrInvalidOperation		= -300;	//!< Execution of the operation would lead to an error/invalid state

	static const EGAVResultCode ErrUnknownUnit			= -200;	//!< Can't instantiate desired EGAV unit (source, sink, input, output, etc.)

	static const EGAVResultCode ErrDeviceInUse			= -108; //!< Device is in use by another application (from EVHALResultCode)
	static const EGAVResultCode ErrInvalidPath			= -101;	//!< File error: specifided path is not valid
	static const EGAVResultCode ErrCouldNotOpenFile		= -100;	//!< File error: Could not open file

	static const EGAVResultCode ErrResultPending		 = -19; //!< hardware busy, try again later
    static const EGAVResultCode ErrResourceNotAvail     =  -18; //!< Resource not available
    static const EGAVResultCode ErrOutOfRange			=  -17;	//!< Out of Range
	static const EGAVResultCode ErrTimeOut				=  -16;	//!< Operation timed out
	static const EGAVResultCode ErrNotSupported			=  -15;	//!< Operation not supported; used with firmware update
	static const EGAVResultCode ErrConversionFailed		=  -14;	//!< Conversion failed
	static const EGAVResultCode ErrNotFound				=  -13;	//!< Not found
	static const EGAVResultCode ErrNoData				=  -12;	//!< No data
	static const EGAVResultCode ErrVideoScaler			=  -11;	//!< Video scaler error
	static const EGAVResultCode ErrEncoder				=  -10;	//!< Encoder error
	static const EGAVResultCode ErrInvalidFormat		=   -9;	//!< Invalid format
	static const EGAVResultCode ErrInvalidParameter		=   -8;	//!< Invalid parameter
	static const EGAVResultCode ErrInvalidState			=   -7;	//!< Invalid state (e.g. when trying to process data while a unit is desinitialized)
	static const EGAVResultCode ErrInsufficientMemory	=   -6;	//!< Out of memory
	static const EGAVResultCode ErrNotInitialized		=   -5;	//!< Not initialized
	static const EGAVResultCode ErrInvalidCast			=   -4;	//!< Cast operation failed
	static const EGAVResultCode ErrNotImplemented		=   -3;	//!< Can't instantiate desired EGAV unit (source, sink, input, output, etc.)
	static const EGAVResultCode ErrNullPointer			=   -2;	//!< Null pointer
	static const EGAVResultCode ErrUnknown				=   -1;	//!< General failure

	static const EGAVResultCode ErrCustom				=    0;	//!< Custom error code:  error code is in mCustomResultCode

	static const EGAVResultCode Ok						=    1;	//!< Success
	static const EGAVResultCode OkNoDataChanged			=    2;	//!< Success: No data were changed (similar to HRESULT value S_FALSE )
	static const EGAVResultCode OkFileNotFound			=    3;	//!< Success: File was not found, but that is a valid state.
	static const EGAVResultCode OkButIncomplete			=    4;	//!< Success: Operation didn't fail, but had some internal uncritical errors (similar to S_FALSE on Windows)

	//------------------------------------------------------------------------------
	// Construction
	//------------------------------------------------------------------------------

	//! Constructor
	EGAVResult() {}
	EGAVResult(EGAVResultCode inResultCode);

	EGAVResult(EGAVResultCustomType inCustomResultType, int64_t inCustomResultCode);

	//------------------------------------------------------------------------------
	// Initialization
	//------------------------------------------------------------------------------
#if _UP_WINDOWS

	//! Init with HRESULT (EGAVResultCustomType::Hresult)
	void InitWithHresult(HRESULT hr);
	//! Init with Windows error code (EGAVResultCustomType::WinError)
	void InitWithWinError(LONG err);
	
#endif

	//------------------------------------------------------------------------------
	// Helpers
	//------------------------------------------------------------------------------

	bool Succeeded() const;
	bool Failed() const { return !Succeeded(); }

	EGAVResultCode GetResultCode() const { return mResultCode; }
	EGAVResultCustomType GetCustomResultType() const { return mCustomResultType; }
	int64_t GetCustomResultCode() const { return mCustomResultCode; }


	void operator=(const EGAVResultCode inResultCode);
	bool operator==(const EGAVResult inResult) const;
	bool operator!=(const EGAVResult inResult) const;
	bool operator==(const EGAVResultCode inResultCode) const;
	bool operator!=(const EGAVResultCode inResultCode) const;

	//------------------------------------------------------------------------------
	// Members
	//------------------------------------------------------------------------------

	// Common
	EGAVResultCode			mResultCode			= ErrCustom;

	// Custom error codes (platform error codes or error codes from other APIs)
	EGAVResultCustomType	mCustomResultType	= EGAVResultCustomType::None;
	int64_t					mCustomResultCode	= 0;

private:
	std::string				mMessage;
};



//==============================================================================
// # Macros
//==============================================================================

#define EGAVResult_CheckPointer(_p_)			\
	{											\
		if (!_p_)								\
			return EGAVResult::ErrNullPointer;	\
	}

#define EGAVResult_CheckCondition(_cond_)		\
	{											\
		if (false == (_cond_))					\
			return EGAVResult::ErrUnknown;		\
	}



#ifndef EGAV_OVERRIDE_DEBUG_MACROS
	inline void dummy() {}

	#define EPL_ASSERT_BREAK(...) dummy()
	#define error_printf(...)     dummy()
	#define warning_printf(...)   dummy()
	#define info_printf(...)      dummy()
#endif
