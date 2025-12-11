# VPGATHER
Using the peculiar behaviour of the VPGATHER instructions to determine if an address will fault before it is truly accessed. All done in user-mode.

## Usage Example
```cpp
#include <Windows.h>
#include "Vpgather.h"

/**
 * @brief Program main subroutine
 * 
 * @param [in] Argc: The number of arguments supplied to the running program
 * @param [in] Argv: A pointer to a list of arguments supplied to the running program, including the name.
 */
LONG
main( 
	_In_ ULONG   Argc, 
	_In_ LPCSTR* Argv 
	)
{
	//
	// Add a vectored exception handler to handle VPGATHER related exceptions.
	//
	AddVectoredExceptionHandler( TRUE, VpgExceptionHandler );

	//
	// This address should never be accessible
	//
	if ( VpgIsAddressAccessible( ( LPVOID )0x1333333333333337 ) == FALSE )
	{
		printf( "0x1333333333333337 is not accessible!\n" );
	}

	//
	// This address should be accessible
	//
	if ( VpgIsAddressAccessible( main ) == TRUE )
	{
		printf( "%p is accessible!\n", main );
	}

	return 0l;
}
```
