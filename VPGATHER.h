//
// File:
//     VPGATHER.h
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
#ifndef __VPGATHER_H__
#define __VPGATHER_H__

#include <Windows.h>
#include <intrin.h>

/**
 * @brief A vectored exception handler responsible for handling VPGATHER
 *        related exceptions.
 * 
 * @param [in] ExceptionPointers: A pointer to an EXCEPTION_POINTERS structure
 * 
 * @return EXCEPTION_CONTINUE_SEARCH If the exception did not concern VPGATHER
 * @return EXCEPTION_CONTINUE_EXECUTION If the exception concerned VPGATHER and was handled successfully
 */
LONG
WINAPI
VpgExceptionHandler( 
	_In_ LPEXCEPTION_POINTERS ExceptionPointers 
	);

/**
 * @brief Use VPGATHER to determine whether an address will incur a fault
 *        before it is actually accessed.
 * 
 * @param [in] Address: The address to test
 * 
 * @return TRUE if the specified address will not incur a fault when accessed.
 * @return FALSE if the specified address will incur a fault when accessed.
 */
BOOLEAN
VpgIsAddressAccessible(
	_In_ LPVOID Address
	);

#endif