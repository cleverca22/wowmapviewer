/*****************************************************************************/
/* SFileOpenArchive.cpp                       Copyright Ladislav Zezula 1999 */
/*                                                                           */
/* Author : Ladislav Zezula                                                  */
/* E-mail : ladik@zezula.net                                                 */
/* WWW    : www.zezula.net                                                   */
/*---------------------------------------------------------------------------*/
/*                       Archive functions of Storm.dll                      */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* xx.xx.xx  1.00  Lad  The first version of SFileOpenArchive.cpp            */
/* 19.11.03  1.01  Dan  Big endian handling                                  */
/*****************************************************************************/

#define __STORMLIB_SELF__
#include "StormLib.h"
#include "SCommon.h"

/*****************************************************************************/
/* Local functions                                                           */
/*****************************************************************************/

static BOOL IsAviFile(TMPQHeader * pHeader)
{
    DWORD * AviHdr = (DWORD *)pHeader;

    // Test for 'RIFF', 'AVI ' or 'LIST'
    return (AviHdr[0] == 0x46464952 && AviHdr[2] == 0x20495641 && AviHdr[3] == 0x5453494C);
}

// This function gets the right positions of the hash table and the block table.
static void RelocateMpqTablePositions(TMPQArchive * ha)
{
    TMPQHeader2 * pHeader = ha->pHeader;

    // Set the proper hash table position
    ha->HashTablePos.HighPart = pHeader->wHashTablePosHigh;
    ha->HashTablePos.LowPart = pHeader->dwHashTablePos;
    ha->HashTablePos.QuadPart += ha->MpqPos.QuadPart;

    // Set the proper block table position
    ha->BlockTablePos.HighPart = pHeader->wBlockTablePosHigh;
    ha->BlockTablePos.LowPart = pHeader->dwBlockTablePos;
    ha->BlockTablePos.QuadPart += ha->MpqPos.QuadPart;

    // Set the proper position of the extended block table
    if(pHeader->ExtBlockTablePos.QuadPart != 0)
    {
        ha->ExtBlockTablePos = pHeader->ExtBlockTablePos;
        ha->ExtBlockTablePos.QuadPart += ha->MpqPos.QuadPart;
    }
}


/*****************************************************************************/
/* Public functions                                                          */
/*****************************************************************************/

//-----------------------------------------------------------------------------
// SFileGetLocale and SFileSetLocale
// Set the locale for all neewly opened archives and files

LCID WINAPI SFileGetLocale()
{
    return lcFileLocale;
}

LCID WINAPI SFileSetLocale(LCID lcNewLocale)
{
    lcFileLocale = lcNewLocale;
    return lcFileLocale;
}

//-----------------------------------------------------------------------------
// SFileOpenArchiveEx (not a public function !!!)
//
//   szFileName - MPQ archive file name to open
//   dwPriority - When SFileOpenFileEx called, this contains the search priority for searched archives
//   dwFlags    - If contains MPQ_OPEN_NO_LISTFILE, then the internal list file will not be used.
//   phMpq      - Pointer to store open archive handle

BOOL SFileOpenArchiveEx(
    const char * szMpqName,
    DWORD dwAccessMode,
    DWORD dwFlags,
    HANDLE * phMpq)
{
    LARGE_INTEGER FileSize = {0};
    TMPQArchive * ha = NULL;            // Archive handle
    HANDLE hFile = INVALID_HANDLE_VALUE;// Opened archive file handle
    DWORD dwMaxBlockIndex = 0;          // Maximum value of block entry
    DWORD dwTransferred;                // Number of bytes read
    DWORD dwBytes = 0;                  // Number of bytes to read
    int nError = ERROR_SUCCESS;   

    // Check the right parameters
    if(szMpqName == NULL || *szMpqName == 0 || phMpq == NULL)
        nError = ERROR_INVALID_PARAMETER;

    // One time initializations of MPQ cryptography
    InitializeMpqCryptography();

    // Open the MPQ archive file
    if(nError == ERROR_SUCCESS)
    {
        hFile = CreateFile(szMpqName, dwAccessMode, FILE_SHARE_READ, NULL, MPQ_OPEN_EXISTING, 0, NULL);
        if(hFile == INVALID_HANDLE_VALUE)
            nError = GetLastError();
    }
    
    // Allocate the MPQhandle
    if(nError == ERROR_SUCCESS)
    {
        FileSize.LowPart = GetFileSize(hFile, (LPDWORD)&FileSize.HighPart);
        if((ha = ALLOCMEM(TMPQArchive, 1)) == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Initialize handle structure and allocate structure for MPQ header
    if(nError == ERROR_SUCCESS)
    {
        memset(ha, 0, sizeof(TMPQArchive));
        strncpy(ha->szFileName, szMpqName, strlen(szMpqName));
        ha->hFile      = hFile;
        ha->pHeader    = &ha->Header;
        ha->pListFile  = NULL;
        hFile = INVALID_HANDLE_VALUE;

        // Remember if the archive is open for write
        if((dwAccessMode & GENERIC_WRITE) == 0)
            ha->dwFlags |= MPQ_FLAG_READ_ONLY;

        // Also remember if we shall check sector CRCs when reading file
        if(dwFlags & MPQ_OPEN_CHECK_SECTOR_CRC)
            ha->dwFlags |= MPQ_FLAG_CHECK_SECTOR_CRC;
    }

    // Find the offset of MPQ header within the file
    if(nError == ERROR_SUCCESS)
    {
        LARGE_INTEGER SearchPos = {0};
        DWORD dwHeaderID;

        for(;;)
        {
            // Invalidate the MPQ ID and read the eventual header
            SetFilePointer(ha->hFile, SearchPos.LowPart, &SearchPos.HighPart, FILE_BEGIN);
            ReadFile(ha->hFile, ha->pHeader, MPQ_HEADER_SIZE_V2, &dwTransferred, NULL);
            dwHeaderID = BSWAP_INT32_UNSIGNED(ha->pHeader->dwID);

            // Special check : Some MPQs are actually AVI files, only with
            // changed extension.
            if(SearchPos.QuadPart == 0 && IsAviFile(ha->pHeader))
            {
                nError = ERROR_AVI_FILE;
                break;
            }

            // If different number of bytes read, break the loop
            if(dwTransferred != MPQ_HEADER_SIZE_V2)
            {
                nError = ERROR_BAD_FORMAT;
                break;
            }

            // If there is the MPQ user data signature, process it
            if(dwHeaderID == ID_MPQ_USERDATA && ha->pUserData == NULL)
            {
                // Ignore the MPQ user data completely if the caller wants to open the MPQ as V1.0
                if((dwFlags & MPQ_OPEN_FORCE_MPQ_V1) == 0)
                {
                    // Fill the user data header
                    ha->pUserData = &ha->UserData;
                    memcpy(ha->pUserData, ha->pHeader, sizeof(TMPQUserData));
                    BSWAP_TMPQUSERDATA(ha->pUserData);

                    // Remember the position of the user data and continue search
                    ha->UserDataPos.QuadPart = SearchPos.QuadPart;
                    SearchPos.QuadPart += ha->pUserData->dwHeaderOffs;
                    continue;
                }
            }

            // There must be MPQ header signature
            if(dwHeaderID == ID_MPQ)
            {
                BSWAP_TMPQHEADER(ha->pHeader);

                // Save the position where the MPQ header has been found
                if(ha->pUserData == NULL)
                    ha->UserDataPos = SearchPos;
                ha->MpqPos = SearchPos;

                // If valid signature has been found, break the loop
                if(ha->pHeader->wFormatVersion == MPQ_FORMAT_VERSION_1)
                {
                    // W3M Map Protectors set some garbage value into the "dwHeaderSize"
                    // field of MPQ header. This value is apparently ignored by Storm.dll
                    if(ha->pHeader->dwHeaderSize != MPQ_HEADER_SIZE_V1)
                    {
                        ha->pHeader->dwHeaderSize = MPQ_HEADER_SIZE_V1;
                        ha->dwFlags |= MPQ_FLAG_PROTECTED;
                    }
					break;
                }

                if(ha->pHeader->wFormatVersion == MPQ_FORMAT_VERSION_2)
                {
                    // W3M Map Protectors set some garbage value into the "dwHeaderSize"
                    // field of MPQ header. This value is apparently ignored by Storm.dll
                    if(ha->pHeader->dwHeaderSize != MPQ_HEADER_SIZE_V2)
                    {
                        ha->pHeader->dwHeaderSize = MPQ_HEADER_SIZE_V2;
                        ha->dwFlags |= MPQ_FLAG_PROTECTED;
                    }
					break;
                }

				//
				// Note: the "dwArchiveSize" member in the MPQ header is ignored by Storm.dll
				// and can contain garbage value ("w3xmaster" protector)
				// 
                
                nError = ERROR_NOT_SUPPORTED;
                break;
            }

            // Move to the next possible offset
            SearchPos.QuadPart += 0x200;
        }
    }

    // Fix table positions according to format
    if(nError == ERROR_SUCCESS)
    {
        // W3x Map Protectors use the fact that War3's Storm.dll ignores the MPQ user data,
        // and probably ignores the MPQ format version as well. The trick is to
        // fake MPQ format 2, with an improper hi-word position of hash table and block table
        // We can overcome such protectors by forcing opening the archive as MPQ v 1.0
        if(dwFlags & MPQ_OPEN_FORCE_MPQ_V1)
        {
            ha->pHeader->wFormatVersion = MPQ_FORMAT_VERSION_1;
            ha->pHeader->dwHeaderSize = MPQ_HEADER_SIZE_V1;
            ha->dwFlags |= MPQ_FLAG_READ_ONLY;
            ha->pUserData = NULL;
        }

        // Clear the fields not supported in older formats
        if(ha->pHeader->wFormatVersion < MPQ_FORMAT_VERSION_2)
        {
            ha->pHeader->ExtBlockTablePos.QuadPart = 0;
            ha->pHeader->wBlockTablePosHigh = 0;
            ha->pHeader->wHashTablePosHigh = 0;
        }

        // Both MPQ_OPEN_NO_LISTFILE or MPQ_OPEN_NO_ATTRIBUTES trigger read only mode
        if(dwFlags & (MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES))
            ha->dwFlags |= MPQ_FLAG_READ_ONLY;

        ha->dwSectorSize = (0x200 << ha->pHeader->wSectorSize);
        RelocateMpqTablePositions(ha);

        // Verify if neither table is outside the file
        if(ha->HashTablePos.QuadPart > FileSize.QuadPart)
            nError = ERROR_BAD_FORMAT;
        if(ha->BlockTablePos.QuadPart > FileSize.QuadPart)
            nError = ERROR_BAD_FORMAT;
        if(ha->ExtBlockTablePos.QuadPart > FileSize.QuadPart)
            nError = ERROR_BAD_FORMAT;
    }

    // Allocate buffers
    if(nError == ERROR_SUCCESS)
    {
        //
        // Note that the block table should be at least as large 
        // as the hash table (for later file additions).
        //
        // Note that the block table *can* be lbigger than the hash table.
        // We have to deal with this properly.
        //
        
        ha->dwBlockTableMax = STORMLIB_MAX(ha->pHeader->dwHashTableSize, ha->pHeader->dwBlockTableSize);

        ha->pHashTable     = ALLOCMEM(TMPQHash, ha->pHeader->dwHashTableSize);
        ha->pBlockTable    = ALLOCMEM(TMPQBlock, ha->dwBlockTableMax);
        ha->pExtBlockTable = ALLOCMEM(TMPQBlockEx, ha->dwBlockTableMax);
        if(!ha->pHashTable || !ha->pBlockTable || !ha->pExtBlockTable)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Read the hash table into memory
    if(nError == ERROR_SUCCESS)
    {
        dwBytes = ha->pHeader->dwHashTableSize * sizeof(TMPQHash);
        SetFilePointer(ha->hFile, ha->HashTablePos.LowPart, &ha->HashTablePos.HighPart, FILE_BEGIN);
        ReadFile(ha->hFile, ha->pHashTable, dwBytes, &dwTransferred, NULL);

        if(dwTransferred != dwBytes)
            nError = ERROR_FILE_CORRUPT;
    }

    // Decrypt hash table and check if it is correctly decrypted
    if(nError == ERROR_SUCCESS)
    {
        TMPQHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
        TMPQHash * pHash;
        BOOL bFoundGoodEntry = FALSE;
        BOOL bFileIsEmpty = TRUE;

        // We have to convert the hash table from LittleEndian
        BSWAP_ARRAY32_UNSIGNED(ha->pHashTable, dwBytes);
        DecryptMpqTable(ha->pHashTable, dwBytes, "(hash table)");

        //
        // Check hash table if is correctly decrypted
        // 
        // Ladik: Some MPQ protectors corrupt the hash table by rewriting part of it.
        // To be able to open these, we will check if there is at least one valid hash table item
        // 

        for(pHash = ha->pHashTable; pHash < pHashEnd; pHash++)
        {
            if(pHash->dwBlockIndex < ha->pHeader->dwBlockTableSize)
                bFileIsEmpty = FALSE;                    

            if(pHash->lcLocale == 0 && pHash->wPlatform == 0 && pHash->dwBlockIndex <= ha->pHeader->dwBlockTableSize)
            {
                bFoundGoodEntry = TRUE;
                break;
            }
        }

        // If we haven't found at least 1 good entry, then the hash table is corrupt
        if(bFileIsEmpty == FALSE && bFoundGoodEntry == FALSE)
            nError = ERROR_FILE_CORRUPT;
    }

    // Now, read the block table
    if(nError == ERROR_SUCCESS)
    {
        // Load the block table to memory
        dwBytes = ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock);
        memset(ha->pBlockTable, 0, (ha->dwBlockTableMax * sizeof(TMPQBlock)));
        SetFilePointer(ha->hFile, ha->BlockTablePos.LowPart, &ha->BlockTablePos.HighPart, FILE_BEGIN);
        ReadFile(ha->hFile, ha->pBlockTable, dwBytes, &dwTransferred, NULL);

        // I have found a MPQ which claimed 0x200 entries in the block table,
        // but the file was cut and there was only 0x1A0 entries.
        // We will handle this case properly, even if that means 
        // omiting another integrity check of the MPQ
        if(dwTransferred < dwBytes)
            dwBytes = dwTransferred;
        BSWAP_ARRAY32_UNSIGNED(ha->pBlockTable, dwBytes);

        // If nothing was read, we assume the file is corrupt.
        if(dwTransferred != dwBytes)
            nError = ERROR_FILE_CORRUPT;
    }

    // Decrypt block table.
    // Some MPQs don't have the block table decrypted, e.g. cracked Diablo version
    // We have to check if block table is really encrypted
    if(nError == ERROR_SUCCESS)
    {
        TMPQBlock * pBlockEnd = ha->pBlockTable + ha->pHeader->dwBlockTableSize;
        TMPQBlock * pBlock = ha->pBlockTable;
        BOOL bBlockTableEncrypted = FALSE;

        // Verify all blocks entries in the table
        // The loop usually stops at the first entry
        while(pBlock < pBlockEnd)
        {
            // The lower 8 bits of the MPQ flags are always zero.
            // Note that this may change in next MPQ versions
            if(pBlock->dwFlags & 0x000000FF)
            {
                bBlockTableEncrypted = TRUE;
                break;
            }

            // Move to the next block table entry
            pBlock++;
        }

        if(bBlockTableEncrypted)
        {
            DecryptMpqTable(ha->pBlockTable, dwBytes, "(block table)");
        }
    }

    // Now, read the extended block table.
    // For V1 archives, we still will maintain the extended block table
    // (it will be filled with zeros)
    if(nError == ERROR_SUCCESS)
    {
        memset(ha->pExtBlockTable, 0, ha->dwBlockTableMax * sizeof(TMPQBlockEx));

        if(ha->pHeader->ExtBlockTablePos.QuadPart != 0)
        {
            dwBytes = ha->pHeader->dwBlockTableSize * sizeof(TMPQBlockEx);
            SetFilePointer(ha->hFile,
                           ha->ExtBlockTablePos.LowPart,
                          &ha->ExtBlockTablePos.HighPart,
                           FILE_BEGIN);
            ReadFile(ha->hFile, ha->pExtBlockTable, dwBytes, &dwTransferred, NULL);

            // We have to convert every WORD in ext block table from LittleEndian
            BSWAP_ARRAY16_UNSIGNED(ha->pExtBlockTable, dwBytes);

            // The extended block table is not encrypted (so far)
            if(dwTransferred != dwBytes)
                nError = ERROR_FILE_CORRUPT;
        }
    }

    // Verify both block tables (If the MPQ file is not protected)
    if(nError == ERROR_SUCCESS && (ha->dwFlags & MPQ_FLAG_PROTECTED) == 0)
    {
        LARGE_INTEGER RawFilePos;
        TMPQBlockEx * pBlockEx = ha->pExtBlockTable;
        TMPQBlock * pBlockEnd = ha->pBlockTable + dwMaxBlockIndex + 1;
        TMPQBlock * pBlock   = ha->pBlockTable;

        // If the MPQ file is not protected,
        // we will check if all sizes in the block table is correct.
        for(; pBlock < pBlockEnd; pBlock++, pBlockEx++)
        {
            if(pBlock->dwFlags & MPQ_FILE_EXISTS)
            {
                // Get the 64-bit file position
                RawFilePos.HighPart = pBlockEx->wFilePosHigh;
                RawFilePos.LowPart = pBlock->dwFilePos;

                // Begin of the file must be within range
                RawFilePos.QuadPart += ha->MpqPos.QuadPart;
                if(RawFilePos.QuadPart > FileSize.QuadPart)
                {
                    nError = ERROR_BAD_FORMAT;
                    break;
                }

                // End of the file must be within range
                RawFilePos.QuadPart += pBlock->dwCSize;
                if(RawFilePos.QuadPart > FileSize.QuadPart)
                {
                    nError = ERROR_BAD_FORMAT;
                    break;
                }
            }
        }
    }

    // If the caller didn't specified otherwise, 
    // include the internal listfile to the TMPQArchive structure
    if(nError == ERROR_SUCCESS && (dwFlags & MPQ_OPEN_NO_LISTFILE) == 0)
    {
        // Create listfile and load it from the MPQ
        nError = SListFileCreateListFile(ha);
        if(nError == ERROR_SUCCESS)
            SFileAddListFile((HANDLE)ha, NULL);
    }

    // If the caller didn't specified otherwise, 
    // load the "(attributes)" file
    if(nError == ERROR_SUCCESS && (dwFlags & MPQ_OPEN_NO_ATTRIBUTES) == 0)
    {
        // Create the attributes file and load it from the MPQ
        nError = SAttrCreateAttributes(ha, 0);
        if(nError == ERROR_SUCCESS)
            SAttrLoadAttributes(ha);
    }

    // Cleanup and exit
    if(nError != ERROR_SUCCESS)
    {
        FreeMPQArchive(ha);
        if(hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
        SetLastError(nError);
        ha = NULL;
    }

    *phMpq = ha;
    return (nError == ERROR_SUCCESS);
}

BOOL WINAPI SFileOpenArchive(const char * szMpqName, DWORD dwPriority, DWORD dwFlags, HANDLE * phMpq)
{
    dwPriority = dwPriority;
    return SFileOpenArchiveEx(szMpqName, GENERIC_READ, dwFlags, phMpq);
}

//-----------------------------------------------------------------------------
// BOOL SFileFlushArchive(HANDLE hMpq)
//
// Saves all dirty data into MPQ archive.
// Has similar effect like SFileCLoseArchive, but the archive is not closed.
// Use on clients who keep MPQ archive open even for write operations,
// and terminating without calling SFileCloseArchive might corrupt the archive.
//

BOOL WINAPI SFileFlushArchive(HANDLE hMpq)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    int nResultError = ERROR_SUCCESS;
    int nError;

    // Do nothing if 'hMpq' is bad parameter
    if(IsValidMpqHandle(ha) == FALSE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    // If the archive has been changed, update the changes on the disk drive.
    // Save listfile (if created), attributes (if created) and also save MPQ tables.
    if(ha->dwFlags & MPQ_FLAG_CHANGED)
    {
        nError = SListFileSaveToMpq(ha);
        if(nError != ERROR_SUCCESS)
            nResultError = nError;

        nError = SAttrFileSaveToMpq(ha);
        if(nError != ERROR_SUCCESS)
            nResultError = nError;

        nError = SaveMPQTables(ha);
        if(nError != ERROR_SUCCESS)
            nResultError = nError;
    }

    // Return the error
    if(nResultError != ERROR_SUCCESS)
        SetLastError(nResultError);
    return (BOOL)(nResultError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// BOOL SFileCloseArchive(HANDLE hMpq);
//

BOOL WINAPI SFileCloseArchive(HANDLE hMpq)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    BOOL bResult;

    // Flush all unsaved data to the storage
    bResult = SFileFlushArchive(hMpq);

    // Free all memory used by MPQ archive
    FreeMPQArchive(ha);
    return bResult;
}

