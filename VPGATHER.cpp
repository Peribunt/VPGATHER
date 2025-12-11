//
// File:
//     VPGATHER.cpp
// 
// Abstract:
//     An interesting way to determine the accessibility or validity of virtual addresses
//     before they are truly accessed. This allows for a much stealther way to determine
//     if an address is accessible, as it does not affect the state or page table entries
//     pointed to by the virtual address. 
// 
//     This proof of concept was made strictly for educational purposes, I am
//     in no way responsible for any software that might implement this functionality
//     for any other purpose.  
//
#include "VPGATHER.h"

//
// Thread local variable stored at PEB->WOW64Reserved. This will serve as a boolean
// for when VPGATHERQQ is found when single stepping.
//
#define TlsVpgatherFound   *( BOOLEAN* )( __readgsqword( 0x30 ) + 0x100 )
//
// Thread local variable stored at PEB->WOW64Reserved + 1. This will serve as boolean
// for when the test address will incur a fault when accessing.
//
#define TlsAddressNotValid *( BOOLEAN* )( __readgsqword( 0x30 ) + 0x101 )

//
// Three-byte VEX/XOP prefix
//
typedef union _XOP_PREFIX
{
	//
	// The possible vector lengths used for the instruction.
	// These values correspond to 'L' in the VEX/XOP prefix union.
	//
#define VEX_LENGTH_128 0
#define VEX_LENGTH_256 1
	//
	// The possible opcode maps used for the instruction.
	// These values correspond to 'map_select' in the VEX/XOP prefix union.
	//
#define VEX_OPCODE_MAP_0F   0b01
#define VEX_OPCODE_MAP_0F38 0b10
#define VEX_OPCODE_MAP_0F3A 0b11
	//
	// Three-byte VEX prefix
	//
#define VEX_PREFIX_VEX3 0xC4
	//
	// The implied mandatory prefix for the opcode.
	// These values correspond to 'pp' in the VEX/XOP prefix union.
	//
#define VEX_PP_NONE 0b00
#define VEX_PP_66   0b01
#define VEX_PP_F3   0b10
#define VEX_PP_F2   0b11

	UINT8 AsUint8[ 3 ];
	struct
	{
		//
		// Byte 1
		//
		UINT8 Prefix; // The first byte is 0xC4 for three-byte VEX prefix,
		              // 0xC5 for the two-byte VEX prefix,
		              // and 0x8F for the three-byte XOP prefix.

		//
		// Byte 2
		//
		UINT8 map_select : 5; // The opcode map to use
		UINT8 B          : 1; // An inverted extension to the ModR/M.reg field
		UINT8 X          : 1; // An inverted extension to the SIB.index  field
		UINT8 R          : 1; // An inverted extension to the ModR/M.reg field

		//
		// Byte 3
		//
		UINT8 pp   : 2; // An implied mandatory prefix for the opcode.
		                // Please refer to the VEX_PP_ definitions
		UINT8 L    : 1; // When 0, 128-bit vectors are used. When 1, 256-bit vectors are used
		UINT8 vvvv : 4; // Additional operand as the inverted encoding for an XMM or YMM register
		UINT8 W_E  : 1; // Equivalent to REX.W. When using integer instructions: when 1, a 64-bit operand size is used
	};
}XOP_PREFIX, *PXOP_PREFIX,
 VEX_PREFIX, *PVEX_PREFIX;

LONG
WINAPI
VpgExceptionHandler( 
	_In_ LPEXCEPTION_POINTERS ExceptionPointers 
	)
{
	CONST PCONTEXT          ContextRecord   = ExceptionPointers->ContextRecord;
	CONST PEXCEPTION_RECORD ExceptionRecord = ExceptionPointers->ExceptionRecord;

	if ( ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT )
	{
		//
		// VPGATHERQQ has not been found yet
		//
		TlsVpgatherFound = FALSE;

		//
		// Start single stepping until VPGATHERQQ is reached.
		//
		ContextRecord->EFlags |= 0x100;
		ContextRecord->Rip    += 1;

		return EXCEPTION_CONTINUE_EXECUTION;
	}
	
	//
	// The VPGATHER instructions will signal a #DB if the test address will incur a fault.
	// For this reason we must single step until this instruction has either completed execution,
	// or has single stepped twice on the same instruction(in which case the #DB was signaled).
	//
	if ( ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP )
	{
		if ( IsBadReadPtr( ( LPVOID )ContextRecord->Rip, 15 ) != FALSE )
		{
			//
			// RIP is not at a valid address
			//
			return EXCEPTION_CONTINUE_SEARCH;
		}

		PVEX_PREFIX VexPrefix = ( PVEX_PREFIX )ContextRecord->Rip;

		if ( *( UINT8* )ContextRecord->Rip == 0x67 )
		{
			//
			// We've encountered an address size override prefix,
			// skip over it.
			//
			VexPrefix = ( PVEX_PREFIX )( ContextRecord->Rip + 1 );

			//
			// Indicate that the test address will incur a fault when accessing.
			//
			TlsAddressNotValid = TRUE;
		}

		if ( 
			 //
			 // Check if the possible VEX prefix of the current instruction
			 // matches up with the criteria for the VPGATHERQQ/VPGATHERDQ instruction.
			 //
			 VexPrefix->Prefix     == VEX_PREFIX_VEX3     &&
			 VexPrefix->L          == VEX_LENGTH_128      &&
			 VexPrefix->pp         == VEX_PP_66           &&
			 VexPrefix->map_select == VEX_OPCODE_MAP_0F38 &&
			 VexPrefix->W_E        == 1                   &&
			
			//
			// Specifically look for the VPGATHERQQ instruction
			// 
			// VPGATHERDQ: VEX.128.66.0F38.W1    90    /r
			// VPGATHERQQ: VEX.128.66.0F38.W1 -> 91 <- /r
			//
			 *( UINT8* )( ContextRecord->Rip + 3 ) == 0x91
		   )
		{
			//
			// VEX.vvvv contains the inverted encoding of the XMM register we need to check.
			// 
			// VPGATHERQQ xmm?, QWORD PTR[???+xmm?], -> xmm? <-
			//
			UINT8 Register = ~( VexPrefix->vvvv ) & 0b1111;

			//
			// Obtain the XMM we need to check from the context record
			//
			PM128A XmmToCheck = &ContextRecord->Xmm0 + Register;

			//
			// If these conditions are met, we have single stepped onto the same instruction
			// and the test address is will incur a fault if accessed.
			//
			if ( TlsVpgatherFound == TRUE && XmmToCheck->Low == 0 )
			{
				//
				// Indicate that the test address is not valid
				//
				TlsAddressNotValid = TRUE;

				//
				// Skip over the current instruction to avoid the fault
				//
				ContextRecord->Rip = ( UINT64 )VexPrefix + 6;
				ContextRecord->Rax = FALSE;

				return EXCEPTION_CONTINUE_EXECUTION;
			}

			if ( TlsVpgatherFound == FALSE )
			{
				//
				// Continue single stepping. If the next #DB lands on the same instruction,
				// we are at a possible faulty address.
				//
				ContextRecord->EFlags |= 0x100;
			}

			//
			// Indicate that we have located our VPGATHERQQ instruction
			//
			TlsVpgatherFound = TRUE;

			return EXCEPTION_CONTINUE_EXECUTION;
		}

		if ( TlsVpgatherFound == FALSE )
		{
			//
			// Continue single stepping until we locate VPGATHERQQ
			//
			ContextRecord->EFlags |= 0x100;
		}

		return EXCEPTION_CONTINUE_EXECUTION;
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

#pragma optimize( push )
#pragma optimize( "", off )
BOOLEAN
VpgIsAddressAccessible(
	_In_ LPVOID Address
	)
{
	//
	// Allocate a single page that is guaranteed to be valid to perform our test.
	//
	LPVOID ValidRegion = VirtualAlloc( NULL, 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

	//
	// Ensure this region is in the working set to avoid unintentional page faults
	//
	VirtualLock( ValidRegion, 1 );

	//
	// Store our test address in the high part of the XMM register.
	// And store the address that is guaranteed to be valid in the low part of the XMM register.
	//
	__m128i Addresses = _mm_set_epi64x( ( UINT64 )Address, ( UINT64 )ValidRegion );

	//
	// This TLS value will be set if the address turns out to be invalid after the test. 
	//
	TlsAddressNotValid = FALSE;

	//
	// Set a breakpoint
	//
	__debugbreak( );

	//
	// Perform VPGATHERQQ
	//
	_mm_i64gather_epi64( NULL, Addresses, 1 );

	//
	// Free our valid address.
	//
	VirtualUnlock( ValidRegion, 1 );
	VirtualFree  ( ValidRegion, NULL, MEM_RELEASE );
	//

	return ( TlsAddressNotValid == FALSE );
}

#pragma optimize( pop )
