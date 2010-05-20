/*****************************************************************************/
/* SAttrFile.cpp                          Copyright (c) Ladislav Zezula 2007 */
/*---------------------------------------------------------------------------*/
/* Description:                                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 12.06.04  1.00  Lad  The first version of SAttrFile.cpp                   */
/*****************************************************************************/

#define __STORMLIB_SELF__
#define __INCLUDE_COMPRESSION__
#define __INCLUDE_CRYPTOGRAPHY__
#include "StormLib.h"
#include "SCommon.h"

//-----------------------------------------------------------------------------
// Local functions

// This function expects the following order of saving internal files:
//
// 1) Save (listfile)
// 2) Save (attributes)
// 3) Save (signature) (not implemented yet)
// 4) Save MPQ tables

static DWORD DetermineFinalBlockTableSize(TMPQArchive * ha)
{
    TMPQBlock * pBlockEnd = ha->pBlockTable + ha->pHeader->dwBlockTableSize;
    TMPQBlock * pBlock;
    DWORD dwExtraEntriesNeeded = 0;

    // Is the (attributes) file in the MPQ already ?
    if(GetHashEntryExact(ha, ATTRIBUTES_NAME, LANG_NEUTRAL) == NULL)
        dwExtraEntriesNeeded++;

    // Is the (signature) file in the MPQ already ?
//  if(GetHashEntryExact(ha, SIGNATURE_NAME, LANG_NEUTRAL) == NULL)
//      dwExtraEntriesNeeded++;

    // If we need some extra entries, check if there's
    // a free space in the middle of the block table
    if(dwExtraEntriesNeeded != 0)
    {
        // Now parse the entire block table and find out if there is enough free entries
        // For each found free entry, we decrement number of extra entries needed
        for(pBlock = ha->pBlockTable; pBlock < pBlockEnd; pBlock++)
        {
            // Is that block table entry free ?
            if((pBlock->dwFlags & MPQ_FILE_EXISTS) == 0)
            {
                if(dwExtraEntriesNeeded == 0)
                    break;
                dwExtraEntriesNeeded--;
            }
        }
    }

    // Now get the number of final table entries
    return ha->pHeader->dwBlockTableSize + dwExtraEntriesNeeded;
}

//-----------------------------------------------------------------------------
// Public functions (internal use by StormLib)

int SAttrCreateAttributes(TMPQArchive * ha, DWORD dwFlags)
{
    TMPQAttributes * pNewAttr;
    int nError = ERROR_SUCCESS;

    // There should NOT be any attributes
    assert(ha->pAttributes == NULL);

    pNewAttr = ALLOCMEM(TMPQAttributes, 1);
    if(pNewAttr == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Pre-set the structure
    pNewAttr->dwVersion = MPQ_ATTRIBUTES_V1;
    pNewAttr->dwFlags = dwFlags;

    // Allocate space for all attributes
    pNewAttr->pFileTime = ALLOCMEM(TMPQFileTime, ha->dwBlockTableMax);
    pNewAttr->pCrc32 = ALLOCMEM(DWORD, ha->dwBlockTableMax);
    pNewAttr->pMd5 = ALLOCMEM(TMPQMD5, ha->dwBlockTableMax);
    if(pNewAttr->pFileTime == NULL || pNewAttr->pCrc32 == NULL || pNewAttr->pMd5 == NULL)
    {
        FreeMPQAttributes(pNewAttr);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    // Initialize all attributes
    memset(pNewAttr->pFileTime, 0, sizeof(TMPQFileTime) * ha->dwBlockTableMax);
    memset(pNewAttr->pCrc32, 0, sizeof(DWORD) * ha->dwBlockTableMax);
    memset(pNewAttr->pMd5, 0, sizeof(TMPQMD5) * ha->dwBlockTableMax);

    // Store the attributes to the MPQ
    ha->pAttributes = pNewAttr;
    return nError;
}


int SAttrLoadAttributes(TMPQArchive * ha)
{
    TMPQAttributes * pAttr = ha->pAttributes;
    HANDLE hFile = NULL;
    DWORD dwBlockTableSize = ha->pHeader->dwBlockTableSize;
    DWORD dwBytesRead;
    DWORD dwToRead;
    int nError = ERROR_SUCCESS;

    // The attributes must be already created
    assert(ha->pAttributes != NULL);
    assert(dwBlockTableSize <= ha->dwBlockTableMax);

    // Attempt to open the "(attributes)" file.
    // If it's not there, then the archive doesn't support attributes
    if(SFileOpenFileEx((HANDLE)ha, ATTRIBUTES_NAME, SFILE_OPEN_ANY_LOCALE, &hFile))
    {
        // Load the content of the attributes file
        dwToRead = sizeof(DWORD) + sizeof(DWORD);
        SFileReadFile(hFile, pAttr, dwToRead, &dwBytesRead, NULL);
        if(dwBytesRead != dwToRead)
            nError = ERROR_FILE_CORRUPT;

        // Verify format of the attributes
        if(nError == ERROR_SUCCESS)
        {
            if(pAttr->dwVersion > MPQ_ATTRIBUTES_V1)
                nError = ERROR_BAD_FORMAT;
        }

        // Load the CRC32 (if any)
        if(nError == ERROR_SUCCESS && (pAttr->dwFlags & MPQ_ATTRIBUTE_CRC32))
        {
            dwToRead = dwBlockTableSize * sizeof(DWORD);
            SFileReadFile(hFile, pAttr->pCrc32, dwToRead, &dwBytesRead, NULL);
            if(dwBytesRead != dwToRead)
                nError = ERROR_FILE_CORRUPT;
        }

        // Read the FILETIMEs (if any)
        // Note: FILETIME array can be incomplete, if it's the last array in the (attributes)
        if(nError == ERROR_SUCCESS && (pAttr->dwFlags & MPQ_ATTRIBUTE_FILETIME))
        {
            dwToRead = dwBlockTableSize * sizeof(TMPQFileTime);
            SFileReadFile(hFile, pAttr->pFileTime, dwToRead, &dwBytesRead, NULL);
            if(dwBytesRead != dwToRead)
                nError = ERROR_FILE_CORRUPT;
        }

        // Read the MD5 (if any)
        // Note: MD5 array can be incomplete, if it's the last array in the (attributes)
        if(nError == ERROR_SUCCESS && (pAttr->dwFlags & MPQ_ATTRIBUTE_MD5))
        {
            dwToRead = dwBlockTableSize * sizeof(TMPQMD5);
            SFileReadFile(hFile, pAttr->pMd5, dwToRead, &dwBytesRead, NULL);
            if(dwBytesRead != dwToRead)
                nError = ERROR_FILE_CORRUPT;
        }

        // 
        // Note: Version 7.00 of StormLib saved the (attributes) incorrectly. 
        // Sometimes, number of entries in the (attributes) was 1 item less
        // than block table size. 
        // If we encounter such table, we will zero all three arrays
        //

        if(nError != ERROR_SUCCESS)
        {
            memset(pAttr->pCrc32,    0, dwBlockTableSize * sizeof(DWORD));
            memset(pAttr->pFileTime, 0, dwBlockTableSize * sizeof(TMPQFileTime));
            memset(pAttr->pMd5,      0, dwBlockTableSize * sizeof(TMPQMD5));
        }

        // Cleanup & exit
        SFileCloseFile(hFile);
    }
    return nError;
}

int SAttrFileSaveToMpq(TMPQArchive * ha)
{
    TMPQAttributes * pAttr = ha->pAttributes;
    DWORD dwFinalBlockTableSize;
    DWORD dwFileSize = 0;
    DWORD dwToWrite;
    void * pvAddHandle = NULL;
    int nError = ERROR_SUCCESS;

    // If there are no attributes, do nothing
    if(ha->pAttributes == NULL)
        return ERROR_SUCCESS;

    // If there are attributes, all three buffers must be valid
    assert(pAttr->pCrc32 != NULL);
    assert(pAttr->pFileTime != NULL);
    assert(pAttr->pMd5 != NULL);

    // We have to determine the block table size that AFTER adding all special MPQ files
    // This is because after attributes file gets saved, the block table size will increase.
    dwFinalBlockTableSize = DetermineFinalBlockTableSize(ha);
    if(dwFinalBlockTableSize > ha->dwBlockTableMax)
        return ERROR_DISK_FULL;

    // Calculate the size of the attributes file
    dwFileSize += sizeof(DWORD) + sizeof(DWORD);     // Header
    if(pAttr->dwFlags & MPQ_ATTRIBUTE_CRC32)
        dwFileSize += dwFinalBlockTableSize * sizeof(DWORD);
    if(pAttr->dwFlags & MPQ_ATTRIBUTE_FILETIME)
        dwFileSize += dwFinalBlockTableSize * sizeof(TMPQFileTime);
    if(pAttr->dwFlags & MPQ_ATTRIBUTE_MD5)
        dwFileSize += dwFinalBlockTableSize * sizeof(TMPQMD5);

    // Create the attributes file in the MPQ
    nError = SFileAddFile_Init(ha, ATTRIBUTES_NAME,
                                   NULL,
                                   dwFileSize,
                                   LANG_NEUTRAL,
                                   MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING,
                                  &pvAddHandle);
    
    // Write all parts of the (attributes) file
    if(nError == ERROR_SUCCESS)
    {
        dwToWrite = sizeof(DWORD) + sizeof(DWORD);
        nError = SFileAddFile_Write(pvAddHandle, pAttr, dwToWrite, MPQ_COMPRESSION_ZLIB);
    }

    // Write the array of CRC32
    if(nError == ERROR_SUCCESS && (pAttr->dwFlags & MPQ_ATTRIBUTE_CRC32))
    {
        dwToWrite = dwFinalBlockTableSize * sizeof(DWORD);
        nError = SFileAddFile_Write(pvAddHandle, pAttr->pCrc32, dwToWrite, MPQ_COMPRESSION_ZLIB);
    }

    // Write the array of FILETIMEs
    if(nError == ERROR_SUCCESS && (pAttr->dwFlags & MPQ_ATTRIBUTE_FILETIME))
    {
        dwToWrite = dwFinalBlockTableSize * sizeof(TMPQFileTime);
        nError = SFileAddFile_Write(pvAddHandle, pAttr->pFileTime, dwToWrite, MPQ_COMPRESSION_ZLIB);
    }

    // Write the array of MD5s
    if(nError == ERROR_SUCCESS && (pAttr->dwFlags & MPQ_ATTRIBUTE_MD5))
    {
        dwToWrite = dwFinalBlockTableSize * sizeof(TMPQMD5);
        nError = SFileAddFile_Write(pvAddHandle, pAttr->pMd5, dwToWrite, MPQ_COMPRESSION_ZLIB);
    }

    // Finalize the file in the archive
    if(pvAddHandle != NULL)
        SFileAddFile_Finish(pvAddHandle, nError);

    return nError;
}

void FreeMPQAttributes(TMPQAttributes * pAttr)
{
    if(pAttr != NULL)
    {
        if(pAttr->pCrc32 != NULL)
            FREEMEM(pAttr->pCrc32);
        if(pAttr->pFileTime != NULL)
            FREEMEM(pAttr->pFileTime);
        if(pAttr->pMd5 != NULL)
            FREEMEM(pAttr->pMd5);

        FREEMEM(pAttr);
    }
}

//-----------------------------------------------------------------------------
// Public functions

BOOL WINAPI SFileCreateAttributes(HANDLE hMpq, DWORD dwFlags)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    int nError;

    // Verify the parameters
    if(!IsValidMpqHandle(ha))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // If the archive already has attributes, return error
    if(ha->pAttributes != NULL)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }

    // Create the attributes
    nError = SAttrCreateAttributes(ha, dwFlags);
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (BOOL)(nError == ERROR_SUCCESS);
}

DWORD WINAPI SFileGetAttributes(HANDLE hMpq)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;

    // Verify the parameters
    if(!IsValidMpqHandle(ha))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return SFILE_INVALID_ATTRIBUTES;
    }

    // If the archive has no attributes, return error
    if(ha->pAttributes == NULL)
    {
        SetLastError(ERROR_CAN_NOT_COMPLETE);
        return SFILE_INVALID_ATTRIBUTES;
    }

    return ha->pAttributes->dwFlags;
}

BOOL WINAPI SFileSetAttributes(HANDLE hMpq, DWORD dwFlags)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;

    // Verify the parameters
    if(!IsValidMpqHandle(ha))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // If the archive has no attributes, return error
    if(ha->pAttributes == NULL)
    {
        SetLastError(ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }

    // Not allowed when the archive is read-only
    if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    // Set the attributes
    ha->pAttributes->dwFlags = (dwFlags & MPQ_ATTRIBUTE_ALL);
    ha->dwFlags |= MPQ_FLAG_CHANGED;
    return TRUE;
}

BOOL WINAPI SFileUpdateFileAttributes(HANDLE hMpq, const char * szFileName)
{
    hash_state md5_state;
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TMPQFile * hf;
    BYTE Buffer[0x1000];
    HANDLE hFile = NULL;
    DWORD dwTotalBytes = 0;
    DWORD dwBytesRead;
    DWORD dwCrc32;

    // Verify the parameters
    if(!IsValidMpqHandle(ha))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Not allowed when the archive is read-only
    if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    // If the archive has no attributes, return error
    if(ha->pAttributes == NULL)
    {
        SetLastError(ERROR_CAN_NOT_COMPLETE);
        return FALSE;
    }

    // Attempt to open the file
    if(!SFileOpenFileEx(hMpq, szFileName, SFILE_OPEN_FROM_MPQ, &hFile))
        return FALSE;

    // Get the file size
    hf = (TMPQFile *)hFile;
    SFileGetFileInfo(hFile, SFILE_INFO_FILE_SIZE, &dwTotalBytes, sizeof(DWORD));

    // Initialize the CRC32 and MD5 contexts
    md5_init(&md5_state);
    dwCrc32 = crc32(0, Z_NULL, 0);

    // Go through entire file and calculate both CRC32 and MD5
    while(dwTotalBytes != 0)
    {
        // Read data from file
        SFileReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
        if(dwBytesRead == 0)
            break;

        // Update CRC32 and MD5
        dwCrc32 = crc32(dwCrc32, Buffer, dwBytesRead);
        md5_process(&md5_state, Buffer, dwBytesRead);

        // Decrement the total size
        dwTotalBytes -= dwBytesRead;
    }

    // Update file CRC32, if it exists
    if(hf->pCrc32 != NULL)
    {
        *hf->pCrc32 = dwCrc32;
    }

    // Update file MD5, if it exists
    if(hf->pMd5 != NULL)
    {
        md5_done(&md5_state, hf->pMd5->Value);
    }

    // Remember that we need to save the MPQ tables
    ha->dwFlags |= MPQ_FLAG_CHANGED;
    SFileCloseFile(hFile);
    return TRUE;
}
