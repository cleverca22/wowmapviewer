/********************************************************************
*
* Description:	implementation for StormLib - Macintosh port
*	
*	these are function wraps to execute Windows API calls
*	as native Macintosh file calls (open/close/read/write/...)
*	requires Mac OS X
*
* Derived from Marko Friedemann <marko.friedemann@bmx-chemnitz.de>
* StormPort.cpp for Linux
*
* Author: Daniel Chiaramello <daniel@chiaramello.net>
*
* Carbonized by: Sam Wilkins <swilkins1337@gmail.com>
*
********************************************************************/

#if (!defined(_WIN32) && !defined(_WIN64))
#include "StormPort.h"
#include "StormLib.h"

static OSErr FSOpenDFCompat(FSRef *ref, char permission, short *refNum)
{
	HFSUniStr255 forkName;
	OSErr theErr;
	Boolean isFolder, wasChanged;
	
	theErr = FSResolveAliasFile(ref, TRUE, &isFolder, &wasChanged);
	if (theErr != noErr)
	{
		return theErr;
	}
	
	FSGetDataForkName(&forkName);
#ifdef PLATFORM_64BIT
	theErr = FSOpenFork(ref, forkName.length, forkName.unicode, permission, (FSIORefNum *)refNum);
#else
	theErr = FSOpenFork(ref, forkName.length, forkName.unicode, permission, refNum);
#endif
	return theErr;
}

static void ConvertUTCDateTimeToFileTime(const UTCDateTimePtr inTime, LPFILETIME outTime)
{
	UInt64 intTime = ((UInt64)inTime->highSeconds << 32) + inTime->lowSeconds;
	intTime *= 10000000;
	intTime += 0x0153b281e0fb4000ull;
	outTime->dwHighDateTime = intTime >> 32;
	outTime->dwLowDateTime = intTime & 0xFFFFFFFF;
}

#pragma mark -

int globalerr;

/********************************************************************
*	 SwapLong
********************************************************************/

uint32_t SwapULong(uint32_t data)
{
	return CFSwapInt32(data);
}

int32_t SwapLong(uint32_t data)
{
	return (int32_t)CFSwapInt32(data);
}

/********************************************************************
*	 SwapShort
********************************************************************/
uint16_t SwapUShort(uint16_t data)
{
	return CFSwapInt16(data);
}

int16_t SwapShort(uint16_t data)
{
	return (int16_t)CFSwapInt16(data);
}

/********************************************************************
*	 ConvertUnsignedLongBuffer
********************************************************************/
void ConvertUnsignedLongBuffer(void * ptr, size_t length)
{
    uint32_t * buffer = (uint32_t *)ptr;
    uint32_t nbLongs = (uint32_t)(length / sizeof(uint32_t));

	while (nbLongs-- > 0)
	{
		*buffer = SwapLong(*buffer);
		buffer++;
	}
}

/********************************************************************
*	 ConvertUnsignedShortBuffer
********************************************************************/
void ConvertUnsignedShortBuffer(void * ptr, size_t length)
{
    uint16_t * buffer = (uint16_t *)ptr;
    uint32_t nbShorts = (uint32_t)(length / sizeof(uint16_t));

    while (nbShorts-- > 0)
	{
		*buffer = SwapShort(*buffer);
		buffer++;
	}
}

/********************************************************************
*	 ConvertTMPQUserData
********************************************************************/
void ConvertTMPQUserData(void *userData)
{
	TMPQUserData * theData = (TMPQUserData *)userData;

	theData->dwID = SwapULong(theData->dwID);
	theData->cbUserDataSize = SwapULong(theData->cbUserDataSize);
	theData->dwHeaderOffs = SwapULong(theData->dwHeaderOffs);
	theData->cbUserDataHeader = SwapULong(theData->cbUserDataHeader);
}

/********************************************************************
*	 ConvertTMPQHeader
********************************************************************/
void ConvertTMPQHeader(void *header)
{
	TMPQHeader2 * theHeader = (TMPQHeader2 *)header;
	
	theHeader->dwID = SwapULong(theHeader->dwID);
	theHeader->dwHeaderSize = SwapULong(theHeader->dwHeaderSize);
	theHeader->dwArchiveSize = SwapULong(theHeader->dwArchiveSize);
	theHeader->wFormatVersion = SwapUShort(theHeader->wFormatVersion);
	theHeader->wSectorSize = SwapUShort(theHeader->wSectorSize);
	theHeader->dwHashTablePos = SwapULong(theHeader->dwHashTablePos);
	theHeader->dwBlockTablePos = SwapULong(theHeader->dwBlockTablePos);
	theHeader->dwHashTableSize = SwapULong(theHeader->dwHashTableSize);
	theHeader->dwBlockTableSize = SwapULong(theHeader->dwBlockTableSize);

	if(theHeader->wFormatVersion >= MPQ_FORMAT_VERSION_2)
	{
		DWORD dwTemp = theHeader->ExtBlockTablePos.LowPart;
		theHeader->ExtBlockTablePos.LowPart = theHeader->ExtBlockTablePos.HighPart;
		theHeader->ExtBlockTablePos.HighPart = dwTemp;
		theHeader->ExtBlockTablePos.LowPart = SwapULong(theHeader->ExtBlockTablePos.LowPart);
		theHeader->ExtBlockTablePos.HighPart = SwapULong(theHeader->ExtBlockTablePos.HighPart);
		theHeader->wHashTablePosHigh = SwapUShort(theHeader->wHashTablePosHigh);
		theHeader->wBlockTablePosHigh = SwapUShort(theHeader->wBlockTablePosHigh);
	}
}

#pragma mark -

/********************************************************************
*	 SetLastError
********************************************************************/
void SetLastError(int err)
{
	globalerr = err;
}

/********************************************************************
*	 GetLastError
********************************************************************/
int GetLastError()
{
	return globalerr;
}

/********************************************************************
*	 ErrString
********************************************************************/
char *ErrString(int err)
{
	switch (err)
	{
		case ERROR_INVALID_FUNCTION:
			return "function not implemented";
		case ERROR_FILE_NOT_FOUND:
			return "file not found";
		case ERROR_ACCESS_DENIED:
			return "access denied";
		case ERROR_NOT_ENOUGH_MEMORY:
			return "not enough memory";
		case ERROR_BAD_FORMAT:
			return "bad format";
		case ERROR_NO_MORE_FILES:
			return "no more files";
		case ERROR_HANDLE_EOF:
			return "access beyond EOF";
		case ERROR_HANDLE_DISK_FULL:
			return "no space left on device";
		case ERROR_INVALID_PARAMETER:
			return "invalid parameter";
		case ERROR_INVALID_HANDLE:
			return "invalid handle";
		case ERROR_DISK_FULL:
			return "no space left on device";
		case ERROR_ALREADY_EXISTS:
			return "file exists";
		case ERROR_CAN_NOT_COMPLETE:
			return "operation cannot be completed";
		case ERROR_INSUFFICIENT_BUFFER:
			return "insufficient buffer";
		case ERROR_WRITE_FAULT:
			return "unable to write to device";
		default:
			return "unknown error";
	}
}

#pragma mark -

/********************************************************************
*	 DeleteFile
*		 lpFileName: file path
********************************************************************/
BOOL DeleteFile(const char * lpFileName)
{
	OSErr	theErr;
	FSRef	theFileRef;
	
	theErr = FSPathMakeRef((const UInt8 *)lpFileName, &theFileRef, NULL);
	if (theErr != noErr)
	{
		SetLastError(theErr);
		return FALSE;
	}
	
	theErr = FSDeleteObject(&theFileRef);
	
	SetLastError(theErr);

	return theErr == noErr;
}

/********************************************************************
*	 MoveFile
*		 lpFromFileName: old file path
*		 lpToFileName: new file path
********************************************************************/
BOOL MoveFile(const char * lpFromFileName, const char * lpToFileName)
{
	OSErr theErr;
	FSRef fromFileRef;
	FSRef toFileRef;
	FSRef parentFolderRef;
	
	// Get the path to the old file
	theErr = FSPathMakeRef((const UInt8 *)lpFromFileName, &fromFileRef, NULL);
	if (theErr != noErr)
	{
		SetLastError(theErr);
		return FALSE;
	}
	
	// Get the path to the new folder for the file
	char folderName[MAX_PATH];
	CFStringRef folderPathCFString = CFStringCreateWithCString(NULL, lpToFileName, kCFStringEncodingUTF8);
	CFURLRef fileURL = CFURLCreateWithFileSystemPath(NULL, folderPathCFString, kCFURLPOSIXPathStyle, FALSE);
	CFURLRef folderURL = CFURLCreateCopyDeletingLastPathComponent(NULL, fileURL);
	CFURLGetFileSystemRepresentation(folderURL, TRUE, (UInt8 *)folderName, MAX_PATH);
	theErr = FSPathMakeRef((UInt8 *)folderName, &parentFolderRef, NULL);
	CFRelease(fileURL);
	CFRelease(folderURL);
	CFRelease(folderPathCFString);
	
	// Move the old file
	theErr = FSMoveObject(&fromFileRef, &parentFolderRef, &toFileRef);
	if (theErr != noErr)
	{
		SetLastError(theErr);
		return FALSE;
	}
	
	// Get a CFString for the new file name
	CFStringRef newFileNameCFString = CFStringCreateWithCString(NULL, lpToFileName, kCFStringEncodingUTF8);
	fileURL = CFURLCreateWithFileSystemPath(NULL, newFileNameCFString, kCFURLPOSIXPathStyle, FALSE);
	CFRelease(newFileNameCFString);
	newFileNameCFString = CFURLCopyLastPathComponent(fileURL);
	CFRelease(fileURL);
	
	// Convert CFString to Unicode and rename the file
	UniChar unicodeFileName[256];
	CFStringGetCharacters(newFileNameCFString, CFRangeMake(0, CFStringGetLength(newFileNameCFString)), 
						  unicodeFileName);
	theErr = FSRenameUnicode(&toFileRef, CFStringGetLength(newFileNameCFString), unicodeFileName, 
							 kTextEncodingUnknown, NULL);
	if (theErr != noErr)
	{
		SetLastError(theErr);
		CFRelease(newFileNameCFString);
		return FALSE;
	}
	
	CFRelease(newFileNameCFString);
	
	SetLastError(theErr);
	return TRUE;
}

/********************************************************************
*	 CreateFile
*		 ulMode: GENERIC_READ | GENERIC_WRITE
*		 ulSharing: FILE_SHARE_READ
*		 pSecAttrib: NULL
*		 ulCreation: MPQ_CREATE_NEW, MPQ_CREATE_ALWAYS, MPQ_OPEN_EXISTING, MPQ_OPEN_ALWAYS
*		 ulFlags: 0
*		 hFile: NULL
********************************************************************/
HANDLE CreateFile(	const char *sFileName,			/* file name */
					DWORD ulMode,					/* access mode */
					DWORD ulSharing,				/* share mode */
					void *pSecAttrib,				/* SD */
					DWORD ulCreation,				/* how to create */
					DWORD ulFlags,					/* file attributes */
					HANDLE hFile	)				/* handle to template file */
{
#pragma unused (ulSharing, pSecAttrib, ulFlags, hFile)

	OSErr	theErr;
	FSRef	theFileRef;
	FSRef	theParentRef;
	short	fileRef;
	char	permission;
	
	theErr = FSPathMakeRef((const UInt8 *)sFileName, &theFileRef, NULL);
	
	if (ulCreation == MPQ_CREATE_NEW && theErr != fnfErr)
	{
		return INVALID_HANDLE_VALUE;
	}
	
	if (ulCreation == MPQ_CREATE_ALWAYS && theErr == noErr)
	{
		FSDeleteObject(&theFileRef);
	}
	
	if (theErr == fnfErr || ulCreation == MPQ_CREATE_ALWAYS)
	{	// Create the FSRef for the parent directory.
		memset(&theFileRef, 0, sizeof(FSRef));
		UInt8 folderName[MAX_PATH];
		CFStringRef folderPathCFString = CFStringCreateWithCString(NULL, sFileName, kCFStringEncodingUTF8);
		CFURLRef fileURL = CFURLCreateWithFileSystemPath(NULL, folderPathCFString, kCFURLPOSIXPathStyle, FALSE);
		CFURLRef folderURL = CFURLCreateCopyDeletingLastPathComponent(NULL, fileURL);
		CFURLGetFileSystemRepresentation(folderURL, TRUE, folderName, MAX_PATH);
		theErr = FSPathMakeRef(folderName, &theParentRef, NULL);
		CFRelease(fileURL);
		CFRelease(folderURL);
		CFRelease(folderPathCFString);
	}
	
	if (theErr != noErr)
	{
		SetLastError(theErr);
		if (ulCreation == MPQ_OPEN_EXISTING || theErr != fnfErr)
			return INVALID_HANDLE_VALUE;
	}
	
	if (ulCreation != MPQ_OPEN_EXISTING && FSGetCatalogInfo(&theFileRef, kFSCatInfoNone, NULL, NULL, NULL, NULL))
	{	/* We create the file */
		UniChar unicodeFileName[256];
		CFStringRef filePathCFString = CFStringCreateWithCString(NULL, sFileName, kCFStringEncodingUTF8);
		CFURLRef fileURL = CFURLCreateWithFileSystemPath(NULL, filePathCFString, kCFURLPOSIXPathStyle, FALSE);
		CFStringRef fileNameCFString = CFURLCopyLastPathComponent(fileURL);
		CFStringGetCharacters(fileNameCFString, CFRangeMake(0, CFStringGetLength(fileNameCFString)), 
							  unicodeFileName);
		theErr = FSCreateFileUnicode(&theParentRef, CFStringGetLength(fileNameCFString), unicodeFileName, 
									 kFSCatInfoNone, NULL, &theFileRef, NULL);
		CFRelease(fileNameCFString);
		CFRelease(filePathCFString);
		CFRelease(fileURL);
		if (theErr != noErr)
		{
			SetLastError(theErr);
			return INVALID_HANDLE_VALUE;
		}
	}

	if (ulMode == GENERIC_READ)
	{
		permission = fsRdPerm;
	}
	else
	{
		if (ulMode == GENERIC_WRITE)
		{
			permission = fsWrPerm;
		}
		else
		{
			permission = fsRdWrPerm;
		}
	}
	theErr = FSOpenDFCompat(&theFileRef, permission, &fileRef);
	
	SetLastError(theErr);

	if (theErr == noErr)
	{
		return (HANDLE)(int)fileRef;
	}
	else
	{
		return INVALID_HANDLE_VALUE;
	}
}

/********************************************************************
*	 CloseHandle
********************************************************************/
BOOL CloseHandle(	HANDLE hFile	)	 /* handle to object */
{
	OSErr theErr;
	
	if ((hFile == NULL) || (hFile == INVALID_HANDLE_VALUE))
	{
		return FALSE;
	}

	theErr = FSCloseFork((short)(long)hFile);
	
	SetLastError(theErr);
	
	return theErr == noErr;
}

/********************************************************************
*	 GetFileSize
********************************************************************/
DWORD GetFileSize(	HANDLE hFile,			/* handle to file */
					DWORD *ulOffSetHigh )	/* high-order word of file size */
{
	SInt64	fileLength;
	OSErr	theErr;

	if ((hFile == NULL) || (hFile == INVALID_HANDLE_VALUE))
	{
		SetLastError(theErr);
		return -1u;
	}
	
	theErr = FSGetForkSize((short)(long)hFile, &fileLength);
	if (theErr != noErr)
	{
		SetLastError(theErr);
		return -1u;
	}
	
	if (ulOffSetHigh != NULL)
	{
		*ulOffSetHigh = fileLength >> 32;
	}

	SetLastError(theErr);
	
	return fileLength;
}

/********************************************************************
*	 SetFilePointer
*		 pOffSetHigh: NULL
*		 ulMethod: FILE_BEGIN, FILE_CURRENT
********************************************************************/
DWORD SetFilePointer(	HANDLE hFile,			/* handle to file */
						LONG lOffSetLow,		/* bytes to move pointer */
						LONG *pOffSetHigh,		/* bytes to move pointer */
						DWORD ulMethod	)		/* starting point */
{
	OSErr theErr;

	if (ulMethod == FILE_CURRENT)
	{
		SInt64	bytesToMove;
		
		if (pOffSetHigh != NULL)
		{
			bytesToMove = ((SInt64)*pOffSetHigh << 32) + lOffSetLow;
		}
		else
		{
			bytesToMove = lOffSetLow;
		}
		
		SInt64	newPos;
		
		theErr = FSSetForkPosition((short)(long)hFile, fsFromMark, bytesToMove);
		if (theErr != noErr)
		{
			SetLastError(theErr);
			return -1u;
		}
		
		theErr = FSGetForkPosition((short)(long)hFile, &newPos);
		if (theErr != noErr)
		{
			SetLastError(theErr);
			return -1u;
		}
		
		if (pOffSetHigh != NULL)
		{
			*pOffSetHigh = newPos >> 32;
		}
		
		SetLastError(theErr);
		return newPos;
	}
	else if (ulMethod == FILE_BEGIN)
	{
		SInt64	bytesToMove;
		
		if (pOffSetHigh != NULL)
		{
			bytesToMove = ((SInt64)*pOffSetHigh << 32) + lOffSetLow;
		}
		else
		{
			bytesToMove = lOffSetLow;
		}
		
		theErr = FSSetForkPosition((short)(long)hFile, fsFromStart, bytesToMove);
		if (theErr != noErr)
		{
			SetLastError(theErr);
			return -1u;
		}
		
		SetLastError(theErr);
		return lOffSetLow;
	}
	else
	{
		SInt64	bytesToMove;
		
		if (pOffSetHigh != NULL)
		{
			bytesToMove = ((SInt64)*pOffSetHigh << 32) + lOffSetLow;
		}
		else
		{
			bytesToMove = lOffSetLow;
		}
		
		SInt64	newPos;
		
		theErr = FSSetForkPosition((short)(long)hFile, fsFromLEOF, bytesToMove);
		if (theErr != noErr)
		{
			SetLastError(theErr);
			return -1u;
		}
		
		theErr = FSGetForkPosition((short)(long)hFile, &newPos);
		if (theErr != noErr)
		{
			SetLastError(theErr);
			return -1u;
		}
		
		if (pOffSetHigh != NULL)
		{
			*pOffSetHigh = newPos >> 32;
		}
		
		SetLastError(theErr);
		return newPos;
	}
}

/********************************************************************
*	 SetEndOfFile
********************************************************************/
BOOL SetEndOfFile(	HANDLE hFile	)	/* handle to file */
{
	OSErr theErr;
	
	theErr = FSSetForkSize((short)(long)hFile, fsAtMark, 0);
	
	SetLastError(theErr);
	
	return theErr == noErr;
}

/********************************************************************
 *	 GetFileTime
 ********************************************************************/
BOOL GetFileTime(	HANDLE hFile,					/* handle to file */
					LPFILETIME lpCreationTime,		/* file creation time */
					LPFILETIME lpLastAccessTime,	/* file accessed time */
					LPFILETIME lpLastWriteTime	)	/* file modified time */
{
	OSErr theErr;
	FSRef theFileRef;
	FSCatalogInfo theCatInfo;
	
	theErr = FSGetForkCBInfo((short)(long)hFile, 0, NULL, NULL, NULL, &theFileRef, NULL);
	if (theErr != noErr)
	{
		SetLastError(theErr);
		return FALSE;
	}
	
	theErr = FSGetCatalogInfo(&theFileRef, kFSCatInfoCreateDate | kFSCatInfoAccessDate | kFSCatInfoContentMod, &theCatInfo, NULL, NULL, NULL);
	
	if (theErr == noErr)
	{
		if (lpCreationTime != NULL)
		{
			ConvertUTCDateTimeToFileTime(&theCatInfo.createDate, lpCreationTime);
		}
		
		if (lpLastAccessTime != NULL)
		{
			ConvertUTCDateTimeToFileTime(&theCatInfo.accessDate, lpLastAccessTime);
		}
		
		if (lpLastWriteTime != NULL)
		{
			ConvertUTCDateTimeToFileTime(&theCatInfo.contentModDate, lpLastWriteTime);
		}		
	}
	
	SetLastError(theErr);
	
	return theErr == noErr;
}

/********************************************************************
*	 ReadFile
*		 pOverLapped: NULL
********************************************************************/
BOOL ReadFile(	HANDLE hFile,			/* handle to file */
				void *pBuffer,			/* data buffer */
				DWORD ulLen,			/* number of bytes to read */
				DWORD *ulRead,			/* number of bytes read */
				void *pOverLapped	)	/* overlapped buffer */
{
#pragma unused (pOverLapped)

	ByteCount	nbCharsRead;
	OSErr		theErr;
	
	nbCharsRead = ulLen;
	theErr = FSReadFork((short)(long)hFile, fsAtMark, 0, nbCharsRead, pBuffer, &nbCharsRead);
	*ulRead = nbCharsRead;
	
	SetLastError(theErr);
	
	return theErr == noErr;
}

/********************************************************************
*	 WriteFile
*		 pOverLapped: NULL
********************************************************************/
BOOL WriteFile( HANDLE hFile,			/* handle to file */
				const void *pBuffer,	/* data buffer */
				DWORD ulLen,			/* number of bytes to write */
				DWORD *ulWritten,		/* number of bytes written */
				void *pOverLapped	)	/* overlapped buffer */
{
#pragma unused (pOverLapped)

	ByteCount	nbCharsToWrite;
	OSErr		theErr;
	
	nbCharsToWrite = ulLen;
	theErr = FSWriteFork((short)(long)hFile, fsAtMark, 0, nbCharsToWrite, pBuffer, &nbCharsToWrite);
	*ulWritten = nbCharsToWrite;
	
	SetLastError(theErr);
		
	return theErr == noErr;
}

// Check if a memory block is accessible for reading. I doubt it's
// possible to check on Mac, so sorry, we'll just have to crash
BOOL IsBadReadPtr(const void * ptr, int size)
{
#pragma unused (ptr, size)

	return FALSE;
}

// Returns attributes of a file. Actually, it doesn't, it just checks if 
// the file exists, since that's all StormLib uses it for
DWORD GetFileAttributes(const char * szFileName)
{
	FSRef		theRef;
	OSErr		theErr;
	
	theErr = FSPathMakeRef((const UInt8 *)szFileName, &theRef, NULL);
	
	if (theErr != noErr)
	{
		return -1u;
	}
	else
	{
		return 0;
	}
}

#endif
