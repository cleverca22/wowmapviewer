/*****************************************************************************/
/* SFileCreateArchiveEx.cpp               Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* MPQ Editing functions                                                     */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 24.03.03  1.00  Lad  Splitted from SFileOpenArchive.cpp                   */
/*****************************************************************************/

#define __STORMLIB_SELF__
#include "StormLib.h"
#include "SCommon.h"

//-----------------------------------------------------------------------------
// Defines

#define DEFAULT_SECTOR_SIZE  3       // Default size of a file sector

BOOL SFileOpenArchiveEx(const char * szMpqName, DWORD dwAccessMode, DWORD dwFlags, HANDLE * phMPQ);

//-----------------------------------------------------------------------------
// Opens or creates a (new) MPQ archive.
//
//  szMpqName - Name of the archive to be created.
//
//  dwCreationDisposition:
//
//   Value              Archive exists         Archive doesn't exist
//   ----------         ---------------------  ---------------------
//   MPQ_CREATE_NEW     Fails                  Creates new archive
//   MPQ_CREATE_ALWAYS  Overwrites existing    Creates new archive
//   MPQ_OPEN_EXISTING  Opens the archive      Fails
//   MPQ_OPEN_ALWAYS    Opens the archive      Creates new archive
//
//   The above mentioned values can be combined with the following flags:
//
//   MPQ_CREATE_ARCHIVE_V1 - Creates MPQ archive version 1
//   MPQ_CREATE_ARCHIVE_V2 - Creates MPQ archive version 2
//   MPQ_CREATE_ATTRIBUTES - Will also add (attributes) file with the CRCs
//   
// dwHashTableSize - Size of the hash table (only if creating a new archive).
//        Must be between 2^4 (= 16) and 2^18 (= 262 144)
//
// phMpq - Receives handle to the archive
//

BOOL WINAPI SFileCreateArchiveEx(const char * szMpqName, DWORD dwFlags, DWORD dwHashTableSize, HANDLE * phMPQ)
{
    LARGE_INTEGER MpqPos = {0};             // Position of MPQ header in the file
    TMPQArchive * ha = NULL;                // MPQ archive handle
    HANDLE hFile = INVALID_HANDLE_VALUE;    // File handle
    USHORT wFormatVersion;
    DWORD dwCreationDisposition;
    DWORD dwBlockTableSize = 0;             // Initial block table size
    DWORD dwPowerOfTwo;
    BOOL bFileExists = FALSE;
    int nError = ERROR_SUCCESS;

    // Check the parameters, if they are valid
    if(szMpqName == NULL || *szMpqName == 0 || phMPQ == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Pre-initialize the result value
    *phMPQ = NULL;

    // Extract format version from the "dwCreationDisposition"
    wFormatVersion = (USHORT)(dwFlags >> 0x10);

    // We only allow those four flags that are defined in StormLib.h
    dwCreationDisposition = (dwFlags & 0x0F);
    if(dwCreationDisposition > MPQ_OPEN_ALWAYS)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Check the value of dwCreationDisposition against file existence
    bFileExists = (GetFileAttributes(szMpqName) != 0xFFFFFFFF);

    // If the file exists and open required, do it.
    if(bFileExists && (dwCreationDisposition == MPQ_OPEN_EXISTING || dwCreationDisposition == MPQ_OPEN_ALWAYS))
    {
        // Try to open the archive normal way. If it fails, it means that
        // the file exist, but it is not a MPQ archive.
        if(SFileOpenArchiveEx(szMpqName, GENERIC_READ | GENERIC_WRITE, dwFlags, phMPQ))
            return TRUE;
        
        // If the caller required to open the existing archive,
        // and the file is not MPQ archive, return error
        if(dwCreationDisposition == MPQ_OPEN_EXISTING)
            return FALSE;
    }

    // Two error cases
    if(dwCreationDisposition == MPQ_CREATE_NEW && bFileExists)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    if(dwCreationDisposition == MPQ_OPEN_EXISTING && bFileExists == FALSE)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    //
    // At this point, we have to create the archive. If the file exists,
    // we will convert it to MPQ archive.
    //

    // Round the hash table size up to the nearest power of two
    for(dwPowerOfTwo = HASH_TABLE_SIZE_MIN; dwPowerOfTwo < dwHashTableSize; dwPowerOfTwo <<= 1);
    if((dwHashTableSize = dwPowerOfTwo) > HASH_TABLE_SIZE_MAX)
        dwHashTableSize = HASH_TABLE_SIZE_MAX;

#ifdef _DEBUG    
    // Debug code, used for testing StormLib
//  dwBlockTableSize = dwHashTableSize * 2;
#endif

    // One time initializations of MPQ cryptography
    InitializeMpqCryptography();

    // Open of create the MPQ archive
    hFile = CreateFile(szMpqName,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ,
                       NULL,
                       dwCreationDisposition,
                       0,
                       NULL);
    if(hFile == INVALID_HANDLE_VALUE)
        nError = GetLastError();

    // Retrieve the file size and round it up to 0x200 bytes
    if(nError == ERROR_SUCCESS)
    {
        MpqPos.LowPart  = GetFileSize(hFile, (LPDWORD)&MpqPos.HighPart);
        MpqPos.QuadPart += 0x1FF;
        MpqPos.LowPart &= 0xFFFFFE00;

        // Move to the end of the file (i.e. begin of the MPQ)
        if(SetFilePointer(hFile, MpqPos.LowPart, &MpqPos.HighPart, FILE_BEGIN) == 0xFFFFFFFF)
            nError = GetLastError();
    }

    // Set the new end of the file to the MPQ header position
    if(nError == ERROR_SUCCESS)
    {
        if(!SetEndOfFile(hFile))
            nError = GetLastError();
    }

    // Create the archive handle
    if(nError == ERROR_SUCCESS)
    {
        if((ha = ALLOCMEM(TMPQArchive, 1)) == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Fill the MPQ archive handle structure and create the header,
    // hash table and block table
    if(nError == ERROR_SUCCESS)
    {
        memset(ha, 0, sizeof(TMPQArchive));
        strcpy(ha->szFileName, szMpqName);
        ha->hFile           = hFile;
        ha->dwSectorSize    = 0x200 << DEFAULT_SECTOR_SIZE;
        ha->UserDataPos     = MpqPos;
        ha->MpqPos          = MpqPos;
        ha->pHeader         = &ha->Header;
        ha->dwBlockTableMax = STORMLIB_MAX(dwHashTableSize, dwBlockTableSize);
        ha->pHashTable      = ALLOCMEM(TMPQHash, dwHashTableSize);
        ha->pBlockTable     = ALLOCMEM(TMPQBlock, ha->dwBlockTableMax);
        ha->pExtBlockTable  = ALLOCMEM(TMPQBlockEx, ha->dwBlockTableMax);
        ha->pListFile       = NULL;
        ha->dwFlags         = 0;

        if(!ha->pHashTable || !ha->pBlockTable || !ha->pExtBlockTable)
            nError = ERROR_NOT_ENOUGH_MEMORY;
        hFile = INVALID_HANDLE_VALUE;
    }

    // Fill the MPQ header and all buffers
    if(nError == ERROR_SUCCESS)
    {
        TMPQHeader2 * pHeader = ha->pHeader;
        DWORD dwHeaderSize = (wFormatVersion == MPQ_FORMAT_VERSION_2) ? MPQ_HEADER_SIZE_V2 : MPQ_HEADER_SIZE_V1;

        memset(pHeader, 0, sizeof(TMPQHeader2));
        pHeader->dwID             = ID_MPQ;
        pHeader->dwHeaderSize     = dwHeaderSize;
        pHeader->dwArchiveSize    = pHeader->dwHeaderSize + dwHashTableSize * sizeof(TMPQHash);
        pHeader->wFormatVersion   = wFormatVersion;
        pHeader->wSectorSize      = DEFAULT_SECTOR_SIZE; // 0x1000 bytes per sector
        pHeader->dwHashTableSize  = dwHashTableSize;
        pHeader->dwBlockTableSize = dwBlockTableSize;

        // Clear all tables
        memset(ha->pHashTable, 0xFF, sizeof(TMPQHash) * dwHashTableSize);
        memset(ha->pBlockTable, 0, sizeof(TMPQBlock) * ha->dwBlockTableMax);
        memset(ha->pExtBlockTable, 0, sizeof(TMPQBlockEx) * ha->dwBlockTableMax);

        // Remember if we shall check sector CRCs when reading file
        if(dwFlags & MPQ_OPEN_CHECK_SECTOR_CRC)
            ha->dwFlags |= MPQ_FLAG_CHECK_SECTOR_CRC;

        //
        // Note: Don't write the MPQ header at this point. If any operation fails later,
        // the unfinished MPQ would stay on the disk, being 0x20 (or 0x2C) bytes long,
        // containing naked MPQ header.
        //

        ha->dwFlags |= MPQ_FLAG_NO_HEADER;

        //
        // Note: Don't recalculate position of MPQ tables at this point.
        // We merely set a flag that indicates that the MPQ tables
        // have been changed, and SaveMpqTables will do the work when closing the archive.
        //

        ha->dwFlags |= MPQ_FLAG_CHANGED;
    }

    // Create the internal listfile
    if(nError == ERROR_SUCCESS)
        nError = SListFileCreateListFile(ha);

    // Create the file attributes
    if(nError == ERROR_SUCCESS && (dwFlags & MPQ_CREATE_ATTRIBUTES))
        nError = SAttrCreateAttributes(ha, MPQ_ATTRIBUTE_ALL);

    // Cleanup : If an error, delete all buffers and return
    if(nError != ERROR_SUCCESS)
    {
        FreeMPQArchive(ha);
        if(hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
        SetLastError(nError);
        ha = NULL;
    }
    
    // Return the values
    *phMPQ = (HANDLE)ha;
    return (nError == ERROR_SUCCESS);
}
