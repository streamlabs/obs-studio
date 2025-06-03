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
@file		EGAVResult.cpp

@brief		Definition of error and success codes
**/
//==============================================================================

#include "EGAVResult.h"
#if _UP_WINDOWS
#include <winerror.h> // for HRESULT
#endif

//------------------------------------------------------------------------------
// Constructor / Destructor
//------------------------------------------------------------------------------

EGAVResult::EGAVResult(EGAVResultCode inResultCode)
{
	mResultCode = inResultCode;
}

EGAVResult::EGAVResult(EGAVResultCustomType inCustomResultType, int64_t inCustomResultCode)
{
	mResultCode       = ErrCustom;
	mCustomResultType = inCustomResultType;
	mCustomResultCode = inCustomResultCode;
}

#if _UP_WINDOWS
void EGAVResult::InitWithHresult(HRESULT hr)
{
	mResultCode       = ErrCustom;
	mCustomResultType = EGAVResultCustomType::Hresult;
	mCustomResultCode = hr;
}

void EGAVResult::InitWithWinError(LONG err)
{
	mResultCode       = ErrCustom;
	mCustomResultType = EGAVResultCustomType::WinError;
	mCustomResultCode = err;
}
#endif

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

bool EGAVResult::Succeeded() const 
{
	if (mResultCode == ErrCustom)
	{
		switch (mCustomResultType)
		{
#if _UP_WINDOWS
		case EGAVResultCustomType::Hresult:		return SUCCEEDED(mCustomResultCode);
		case EGAVResultCustomType::WinError:	return (mCustomResultCode == ERROR_SUCCESS) ? true : false;
#elif _UP_MAC
		case EGAVResultCustomType::Mac:			return (mCustomResultCode == 0) ? true : false;
#endif
		case EGAVResultCustomType::MainConcept:
		case EGAVResultCustomType::Device:
			return (mCustomResultCode == 0) ? true : false;
		default:
			return false;
		}
	}
	else
	{
		return (int32_t)mResultCode > 0 ? true : false;
	}
}


//------------------------------------------------------------------------------
// Operators
//------------------------------------------------------------------------------
void EGAVResult::operator=(const EGAVResultCode inResultCode)
{
	mCustomResultType = EGAVResultCustomType::None;
	mResultCode = inResultCode;
}

bool EGAVResult::operator==(const EGAVResult inResult) const
{
	if (mResultCode 	  != inResult.mResultCode	   ) return false;
	if (mCustomResultType != inResult.mCustomResultType) return false;
	if (mCustomResultCode != inResult.mCustomResultCode) return false;
	return true;
}

bool EGAVResult::operator!=(const EGAVResult inResult) const
{
	return !(*this == inResult);
}


bool EGAVResult::operator==(const EGAVResultCode inResultCode) const
{
	return mResultCode == inResultCode;
}

bool EGAVResult::operator!=(const EGAVResultCode inResultCode) const
{
	return mResultCode != inResultCode;
}
