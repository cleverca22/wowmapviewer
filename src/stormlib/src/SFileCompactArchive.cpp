/*****************************************************************************/
/* SFileCompactArchive.cpp                Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* Archive compacting function                                               */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 14.04.03  1.00  Lad  Splitted from SFileCreateArchiveEx.cpp               */
/* 19.11.03  1.01  Dan  Big endian handling                                  */
/*****************************************************************************/

#define __STORMLIB_SELF__
#include "StormLib.h"
#include "SCommon.h"

/*****************************************************************************/
/* Local variables                                                           */
/*****************************************************************************/

static SFILE_COMPACT_CALLBACK CompactCB = NULL;
static LARGE_INTEGER CompactBytesProcessed = {0};
static LARGE_INTEGER CompactTotalBytes = {0};
static void * pvUserData = NULL;

/*****************************************************************************/
/* Local functions                                                           */
/*****************************************************************************/

// Creates a copy of hash table
static TMPQHash * CopyHashTable(TMPQArchive * ha)
{
    TMPQHash * pHashTableCopy = ALLOCMEM(TMPQHash, ha->pHeader->dwHashTableSize);

    if(pHashTableCopy != NULL)
        memcpy(pHashTableCopy, ha->pHashTable, sizeof(TMPQHash) * ha->pHeader->dwHashTableSize);

    return pHashTableCopy;
}

static int ReallocateBlockTable(TMPQArchive * ha, DWORD dwNewTableSize)
{
    TMPQFileTime * pFileTime;
    TMPQBlockEx * pExtBlockTable;
    TMPQBlock * pBlockTable;
    TMPQMD5 * pMd5;
    DWORD * pCrc32;

    // Reallocate the the block table
    pBlockTable = ALLOCMEM(TMPQBlock, dwNewTableSize);
    if(pBlockTable == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(pBlockTable, 0, dwNewTableSize * sizeof(TMPQBlock));
    memcpy(pBlockTable, ha->pBlockTable, ha->dwBlockTableMax * sizeof(TMPQBlock));
    FREEMEM(ha->pBlockTable);
    ha->pBlockTable = pBlockTable;

    // Reallocate the extended block table
    pExtBlockTable = ALLOCMEM(TMPQBlockEx, dwNewTableSize);
    if(pExtBlockTable == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(pExtBlockTable, 0, dwNewTableSize * sizeof(TMPQBlockEx));
    memcpy(pExtBlockTable, ha->pExtBlockTable, ha->dwBlockTableMax * sizeof(TMPQBlockEx));
    FREEMEM(ha->pExtBlockTable);
    ha->pExtBlockTable = pExtBlockTable;

    // We also have to reallocate the (attributes) data
    if(ha->pAttributes != NULL)
    {
        // Reallocate the array of file CRCs
        pCrc32 = ALLOCMEM(DWORD, dwNewTableSize);
        if(pCrc32 == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(pCrc32, 0, dwNewTableSize * sizeof(DWORD));
        memcpy(pCrc32, ha->pAttributes->pCrc32, ha->dwBlockTableMax * sizeof(DWORD));
        FREEMEM(ha->pAttributes->pCrc32);
        ha->pAttributes->pCrc32 = pCrc32;

        // Reallocate the array of file times
        pFileTime = ALLOCMEM(TMPQFileTime, dwNewTableSize);
        if(pFileTime == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(pFileTime, 0, dwNewTableSize * sizeof(TMPQFileTime));
        memcpy(pFileTime, ha->pAttributes->pFileTime, ha->dwBlockTableMax * sizeof(TMPQFileTime));
        FREEMEM(ha->pAttributes->pFileTime);
        ha->pAttributes->pFileTime = pFileTime;

        // Reallocate the array of file MD5
        pMd5 = ALLOCMEM(TMPQMD5, dwNewTableSize);
        if(pMd5 == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(pMd5, 0, dwNewTableSize * sizeof(TMPQMD5));
        memcpy(pMd5, ha->pAttributes->pMd5, ha->dwBlockTableMax * sizeof(TMPQMD5));
        FREEMEM(ha->pAttributes->pMd5);
        ha->pAttributes->pMd5 = pMd5;
    }

    // If all succeeded, remember the new size of the tables
    ha->dwBlockTableMax = dwNewTableSize;
    return ERROR_SUCCESS;
}

static int CheckIfAllFilesKnown(TMPQArchive * ha, const char * szListFile, DWORD * pFileKeys)
{
    TMPQHash * pHashTableCopy = NULL;   // Copy of the hash table
    TMPQHash * pHash;
    TMPQHash * pHashEnd = NULL;         // End of the hash table
    DWORD dwFileCount = 0;
    BOOL bHasUnknownNames = FALSE;
    int nError = ERROR_SUCCESS;

    // First of all, create a copy of hash table
    if(nError == ERROR_SUCCESS)
    {
        if((pHashTableCopy = CopyHashTable(ha)) == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
        pHashEnd = pHashTableCopy + ha->pHeader->dwHashTableSize;

        // Notify the user
        if(CompactCB != NULL)
            CompactCB(pvUserData, CCB_CHECKING_FILES, &CompactBytesProcessed, &CompactTotalBytes);
    }
                
    // Now check all the files from the listfile
    if(nError == ERROR_SUCCESS)
    {
        SFILE_FIND_DATA wf;
        HANDLE hFind = SFileFindFirstFile((HANDLE)ha, "*", &wf, szListFile);
        BOOL bResult = TRUE;

        // Do while something has been found
        while(hFind != NULL && bResult)
        {
            TMPQHash * pHash = ha->pHashTable + wf.dwHashIndex;

            // Find the entry in the hash table copy
            pHash = pHashTableCopy + (pHash - ha->pHashTable);
            if(pHash->dwBlockIndex != HASH_ENTRY_FREE)
            {
                TMPQBlock * pBlock = ha->pBlockTable + pHash->dwBlockIndex;
                DWORD dwFileKey = 0;

                // Resolve the file key. Use plain file name for it
                if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
                {
                    const char * szPlainName = GetPlainMpqFileName(wf.cFileName);

                    dwFileKey = DecryptFileKey(szPlainName);
                    if(pBlock->dwFlags & MPQ_FILE_FIX_KEY)
                        dwFileKey = (dwFileKey + pBlock->dwFilePos) ^ pBlock->dwFSize;
                }

                // Give the key to the caller
                pFileKeys[pHash->dwBlockIndex] = dwFileKey;

                pHash->dwName1      = 0xFFFFFFFF;
                pHash->dwName2      = 0xFFFFFFFF;
                pHash->lcLocale     = 0xFFFF;
                pHash->wPlatform    = 0xFFFF;
                pHash->dwBlockIndex = HASH_ENTRY_FREE;
            }

            // Find the next file in the archive
            bResult = SFileFindNextFile(hFind, &wf);
        }

        if(hFind != NULL)
            SFileFindClose(hFind);
    }

    // When the filelist checking is complete, parse the hash table copy and find the
    if(nError == ERROR_SUCCESS)
    {
        // Notify the user about checking hash table
        dwFileCount = 0;
        if(CompactCB != NULL)
            CompactCB(pvUserData, CCB_CHECKING_HASH_TABLE, &CompactBytesProcessed, &CompactTotalBytes);

        for(pHash = pHashTableCopy; pHash < pHashEnd; pHash++)
        {
            // If there is an unresolved entry, try to detect its key. If it fails,
            // we cannot complete the work
            if(pHash->dwBlockIndex < ha->pHeader->dwBlockTableSize)
            {
                HANDLE hFile  = NULL;
                DWORD dwFlags = 0;
                DWORD dwFileKey  = 0;

                // Relelber that we have at least one name that is not known
                bHasUnknownNames = TRUE;

                if(SFileOpenFileEx((HANDLE)ha, (char *)(DWORD_PTR)pHash->dwBlockIndex, SFILE_OPEN_BY_INDEX, &hFile))
                {
                    TMPQFile * hf = (TMPQFile *)hFile;
                    dwFlags = hf->pBlock->dwFlags;
                    dwFileKey = hf->dwFileKey;
                    SFileCloseFile(hFile);
                }

                // If the file is encrypted, we have to check 
                // If we can apply the file decryption key
                if((dwFlags & MPQ_FILE_ENCRYPTED) && dwFileKey == 0)
                {
                    nError = ERROR_CAN_NOT_COMPLETE;
                    break;
                }

                // Give the key to the caller
                pFileKeys[pHash->dwBlockIndex] = dwFileKey;
            }
        }
    }

    // Delete the copy of hash table
    if(pHashTableCopy != NULL)
        FREEMEM(pHashTableCopy);
    return nError;
}

static int CopyNonMpqData(
    HANDLE hSrcFile,
    HANDLE hTrgFile,
    LARGE_INTEGER & ByteOffset,
    LARGE_INTEGER & ByteCount)
{
    LARGE_INTEGER DataSize = ByteCount;
    DWORD dwTransferred;
    DWORD dwToRead;
    char DataBuffer[0x1000];
    int nError = ERROR_SUCCESS;

    // Ensure that the source file pointer is at the proper position
    // Note: Target file pointer is assumed to be set already
    SetFilePointer(hSrcFile, ByteOffset.LowPart, (PLONG)&ByteOffset.HighPart, FILE_BEGIN);

    // Copy the data
    while(DataSize.QuadPart > 0)
    {
        // Get the proper size of data
        dwToRead = sizeof(DataBuffer);
        if(DataSize.HighPart == 0 && DataSize.LowPart < dwToRead)
            dwToRead = DataSize.LowPart;

        // Read the source file
        ReadFile(hSrcFile, DataBuffer, dwToRead, &dwTransferred, NULL);
        if(dwTransferred != dwToRead)
        {
            nError = ERROR_CAN_NOT_COMPLETE;
            break;
        }

        // Write to the target file
        WriteFile(hTrgFile, DataBuffer, dwToRead, &dwTransferred, NULL);
        if(dwTransferred != dwToRead)
        {
            nError = ERROR_DISK_FULL;
            break;
        }

        // Update the progress
        if(CompactCB != NULL)
        {
            CompactBytesProcessed.QuadPart += dwTransferred;
            CompactCB(pvUserData, CCB_COPYING_NON_MPQ_DATA, &CompactBytesProcessed, &CompactTotalBytes);
        }

        // Decrement the number of data to be copied
        DataSize.QuadPart -= dwTransferred;
    }

    return ERROR_SUCCESS;
}

// Copies all file sectors into another archive.
static int CopyMpqFileSectors(
    TMPQArchive * ha,
    TMPQFile * hf,
    HANDLE hNewFile)
{
    LARGE_INTEGER RawFilePos;           // Used for calculating sector offset in the old MPQ archive
    LARGE_INTEGER MpqFilePos;           // MPQ file position in the new archive
    TMPQBlockEx * pBlockEx = hf->pBlockEx;
    TMPQBlock * pBlock = hf->pBlock;
    DWORD dwBytesToCopy = pBlock->dwCSize;
    DWORD dwTransferred = 0;            // Number of bytes transferred
    DWORD dwFileKey1 = 0;               // File key used for decryption
    DWORD dwFileKey2 = 0;               // File key used for encryption
    DWORD dwCSize = 0;                  // Compressed file size
    int nError = ERROR_SUCCESS;

    // Remember the position in the destination file
    MpqFilePos.HighPart = 0;
    MpqFilePos.LowPart = SetFilePointer(hNewFile, 0, &MpqFilePos.HighPart, FILE_CURRENT);
    MpqFilePos.QuadPart -= ha->MpqPos.QuadPart;

    // Resolve decryption keys. Note that the file key given 
    // in the TMPQFile structure also includes the key adjustment
    if(nError == ERROR_SUCCESS && (pBlock->dwFlags & MPQ_FILE_ENCRYPTED))
    {
        dwFileKey2 = dwFileKey1 = hf->dwFileKey;
        if(pBlock->dwFlags & MPQ_FILE_FIX_KEY)
        {
            dwFileKey2 = (dwFileKey1 ^ pBlock->dwFSize) - pBlock->dwFilePos;
            dwFileKey2 = (dwFileKey2 + MpqFilePos.LowPart) ^ pBlock->dwFSize;
        }
    }

    // If we have to save sector offset table, do it.
    if(nError == ERROR_SUCCESS && hf->SectorOffsets != NULL)
    {
        DWORD * SectorOffsetsCopy = ALLOCMEM(DWORD, hf->dwSectorCount);
        DWORD dwSectorPosLen = hf->dwSectorCount * sizeof(DWORD);

        assert((pBlock->dwFlags & MPQ_FILE_SINGLE_UNIT) == 0);
        assert(pBlock->dwFlags & MPQ_FILE_COMPRESSED);

        if(SectorOffsetsCopy == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;

        // Encrypt the secondary sector offset table and write it to the target file
        if(nError == ERROR_SUCCESS)
        {
            memcpy(SectorOffsetsCopy, hf->SectorOffsets, dwSectorPosLen);
            if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
                EncryptMpqBlock(SectorOffsetsCopy, dwSectorPosLen, dwFileKey2 - 1);

            BSWAP_ARRAY32_UNSIGNED(SectorOffsetsCopy, dwSectorPosLen);
            WriteFile(hNewFile, SectorOffsetsCopy, dwSectorPosLen, &dwTransferred, NULL);
            if(dwTransferred != dwSectorPosLen)
                nError = ERROR_DISK_FULL;

            dwCSize += dwTransferred;
        }

        // Update compact progress
        if(CompactCB != NULL)
        {
            CompactBytesProcessed.QuadPart += dwTransferred;
            CompactCB(pvUserData, CCB_COMPACTING_FILES, &CompactBytesProcessed, &CompactTotalBytes);
        }

        FREEMEM(SectorOffsetsCopy);
    }

    // Now we have to copy all file sectors. We do it without
    // recompression, because recompression is not necessary in this case
    if(nError == ERROR_SUCCESS)
    {
        for(DWORD dwSector = 0; dwSector < hf->dwDataSectors; dwSector++)
        {
            DWORD dwRawDataInSector = hf->dwSectorSize;
            DWORD dwRawByteOffset = dwSector * hf->dwSectorSize;

            // Last sector: If there is not enough bytes remaining in the file, cut the raw size
            if(dwRawDataInSector > dwBytesToCopy)
                dwRawDataInSector = dwBytesToCopy;

            // Fix the raw data length if the file is compressed
            if(hf->SectorOffsets != NULL)
            {
                dwRawDataInSector = hf->SectorOffsets[dwSector+1] - hf->SectorOffsets[dwSector];
                dwRawByteOffset = hf->SectorOffsets[dwSector];
            }

            // Calculate the raw file offset of the file sector
            CalculateRawSectorOffset(RawFilePos, hf, dwRawByteOffset);
            SetFilePointer(ha->hFile, RawFilePos.LowPart, &RawFilePos.HighPart, FILE_BEGIN);

            // Read the file sector
            ReadFile(ha->hFile, hf->pbFileSector, dwRawDataInSector, &dwTransferred, NULL);
            if(dwTransferred != dwRawDataInSector)
            {
                nError = ERROR_FILE_CORRUPT;
                break;
            }

            // If necessary, re-encrypt the sector
            // Note: Recompression is not necessary here. Unlike encryption, 
            // the compression does not depend on the position of the file in MPQ.
            if((pBlock->dwFlags & MPQ_FILE_ENCRYPTED) && dwFileKey1 != dwFileKey2)
            {
                BSWAP_ARRAY32_UNSIGNED(hf->pbFileSector, dwRawDataInSector);
                DecryptMpqBlock(hf->pbFileSector, dwRawDataInSector, dwFileKey1 + dwSector);
                EncryptMpqBlock(hf->pbFileSector, dwRawDataInSector, dwFileKey2 + dwSector);
                BSWAP_ARRAY32_UNSIGNED(hf->pbFileSector, dwRawDataInSector);
            }

            // Now write the sector back to the file
            WriteFile(hNewFile, hf->pbFileSector, dwRawDataInSector, &dwTransferred, NULL);
            if(dwTransferred != dwRawDataInSector)
            {
                nError = ERROR_DISK_FULL;
                break;
            }

            // Update compact progress
            if(CompactCB != NULL)
            {
                CompactBytesProcessed.QuadPart += dwTransferred;
                CompactCB(pvUserData, CCB_COMPACTING_FILES, &CompactBytesProcessed, &CompactTotalBytes);
            }

            // Adjust byte counts
            dwBytesToCopy -= hf->dwSectorSize;
            dwCSize += dwTransferred;
        }
    }

    // Copy the sector CRCs, if any
    // Sector CRCs are always compressed (not imploded) and unencrypted
    if(nError == ERROR_SUCCESS && hf->SectorOffsets != NULL && hf->SectorChksums != NULL)
    {
        DWORD dwCrcLength;

        dwCrcLength = hf->SectorOffsets[hf->dwSectorCount - 1] - hf->SectorOffsets[hf->dwSectorCount - 2];
        if(dwCrcLength != 0)
        {
            ReadFile(ha->hFile, hf->SectorChksums, dwCrcLength, &dwTransferred, NULL);
            if(dwTransferred != dwCrcLength)
                nError = ERROR_FILE_CORRUPT;

            WriteFile(hNewFile, hf->SectorChksums, dwCrcLength, &dwTransferred, NULL);
            if(dwTransferred != dwCrcLength)
                nError = ERROR_DISK_FULL;

            // Update compact progress
            if(CompactCB != NULL)
            {
                CompactBytesProcessed.QuadPart += dwTransferred;
                CompactCB(pvUserData, CCB_COMPACTING_FILES, &CompactBytesProcessed, &CompactTotalBytes);
            }

            // Size of the CRC block is also included in the compressed file size
            dwCSize += dwCrcLength;
        }
    }

    // Update file position in the block table
    if(nError == ERROR_SUCCESS)
    {
        // At this point, number of bytes written should be exactly
        // the same like the compressed file size. If it isn't,
        // there's something wrong (an unknown archive version, MPQ protection, ...)
        // 
        // Note: Diablo savegames have very weird layout, and the file "hero"
        // seems to have improper compressed size. Instead of real compressed size,
        // the "dwCSize" member of the block table entry contains
        // uncompressed size of file data + size of the sector table.
        // If we compact the archive, Diablo will refuse to load the game
        // Seems like some sort of protection to me.
        if(dwCSize == pBlock->dwCSize)
        {
            // Update file pos in the block table
            pBlockEx->wFilePosHigh = (USHORT)MpqFilePos.HighPart;
            pBlock->dwFilePos = MpqFilePos.LowPart;
        }
        else
        {
            nError = ERROR_FILE_CORRUPT;
            assert(FALSE);
        }
    }

    return nError;
}

static int CopyMpqFiles(TMPQArchive * ha, DWORD * pFileKeys, HANDLE hNewFile)
{
    TMPQBlockEx * pBlockEx;    
    TMPQBlock * pBlock;    
    TMPQFile * hf = NULL;
    DWORD dwIndex;
    int nError = ERROR_SUCCESS;

    // Walk through all files and write them to the destination MPQ archive
    for(dwIndex = 0; dwIndex < ha->pHeader->dwBlockTableSize; dwIndex++)
    {
        pBlockEx = ha->pExtBlockTable + dwIndex;
        pBlock = ha->pBlockTable + dwIndex;

        // Copy all the file sectors
        // Only do that when the file has nonzero size
        if((pBlock->dwFlags & MPQ_FILE_EXISTS) && pBlock->dwFSize != 0)
        {
            // Allocate structure for the MPQ file
            hf = CreateMpqFile(ha, "<compacting>");
            if(hf == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;

            // Store block positions
            hf->pBlockEx = pBlockEx;
            hf->pBlock = pBlock;

            // Set the raw file position
            hf->MpqFilePos.HighPart = hf->pBlockEx->wFilePosHigh;
            hf->MpqFilePos.LowPart  = hf->pBlock->dwFilePos;
            hf->RawFilePos.QuadPart = ha->MpqPos.QuadPart + hf->MpqFilePos.LowPart;

            // Set the file decryption key
            hf->dwFileKey = pFileKeys[dwIndex];

            // Allocate buffers for file sector and sector offset table
            nError = AllocateSectorBuffer(hf);
            if(nError != ERROR_SUCCESS)
                break;

            // Also allocate sector offset table and sector checksum table
            nError = AllocateSectorOffsets(hf, true);
            if(nError != ERROR_SUCCESS)
                break;

            if(pBlock->dwFlags & MPQ_FILE_SECTOR_CRC)
            {
                nError = AllocateSectorChecksums(hf, false);
                if(nError != ERROR_SUCCESS)
                    break;
            }

            // Copy all file sectors
            nError = CopyMpqFileSectors(ha, hf, hNewFile);
            if(nError != ERROR_SUCCESS)
                break;

            // Free buffers. This also sets "hf" to NULL.
            FreeMPQFile(hf);
        }
    }

    // Cleanup and exit
    if(hf != NULL)
        FreeMPQFile(hf);
    return nError;
}


/*****************************************************************************/
/* Public functions                                                          */
/*****************************************************************************/

BOOL WINAPI SFileSetCompactCallback(HANDLE /* hMpq */, SFILE_COMPACT_CALLBACK aCompactCB, void * pvData)
{
    CompactCB = aCompactCB;
    pvUserData = pvData;
    return TRUE;
}

//-----------------------------------------------------------------------------
// Archive compacting

BOOL WINAPI SFileCompactArchive(HANDLE hMpq, const char * szListFile, BOOL /* bReserved */)
{
    LARGE_INTEGER ByteOffset;
    LARGE_INTEGER ByteCount;
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    HANDLE hNewFile = INVALID_HANDLE_VALUE;
    DWORD * pFileKeys = NULL;
    char szTempFile[MAX_PATH] = "";
    char * szTemp = NULL;
    DWORD dwTransferred;
    int nError = ERROR_SUCCESS;

    // Test the valid parameters
    if(IsValidMpqHandle(ha) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
        nError = ERROR_ACCESS_DENIED;

    // Create the table with file keys
    if(nError == ERROR_SUCCESS)
    {
        if((pFileKeys = ALLOCMEM(DWORD, ha->pHeader->dwBlockTableSize)) != NULL)
            memset(pFileKeys, 0, sizeof(DWORD) * ha->pHeader->dwBlockTableSize);
        else
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // First of all, we have to check of we are able to decrypt all files.
    // If not, sorry, but the archive cannot be compacted.
    if(nError == ERROR_SUCCESS)
    {
        // Initialize the progress variables for compact callback
        CompactTotalBytes.LowPart = GetFileSize(ha->hFile, (LPDWORD)&CompactTotalBytes.HighPart);
        CompactBytesProcessed.QuadPart = 0;
        nError = CheckIfAllFilesKnown(ha, szListFile, pFileKeys);
    }

    // Get the temporary file name and create it
    if(nError == ERROR_SUCCESS)
    {
        strcpy(szTempFile, ha->szFileName);
        if((szTemp = strrchr(szTempFile, '.')) != NULL)
            strcpy(szTemp + 1, "mp_");
        else
            strcat(szTempFile, "_");

        hNewFile = CreateFile(szTempFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, MPQ_CREATE_ALWAYS, 0, NULL);
        if(hNewFile == INVALID_HANDLE_VALUE)
            nError = GetLastError();
    }

    // Write the data before MPQ user data (if any)
    if(nError == ERROR_SUCCESS && ha->UserDataPos.QuadPart != 0)
    {
        // Inform the application about the progress
        if(CompactCB != NULL)
            CompactCB(pvUserData, CCB_COPYING_NON_MPQ_DATA, &CompactBytesProcessed, &CompactTotalBytes);

        ByteOffset.QuadPart = 0;
        ByteCount.QuadPart = ha->UserDataPos.QuadPart;
        nError = CopyNonMpqData(ha->hFile, hNewFile, ByteOffset, ByteCount);
    }

    // Write the MPQ user data (if any)
    if(nError == ERROR_SUCCESS && ha->MpqPos.QuadPart > ha->UserDataPos.QuadPart)
    {
        // At this point, we assume that the user data size is equal
        // to pUserData->dwHeaderOffs.
        // If this assumption doesn't work, then we have an unknown version of MPQ
        ByteOffset.QuadPart = ha->UserDataPos.QuadPart;
        ByteCount.QuadPart = ha->MpqPos.QuadPart - ha->UserDataPos.QuadPart;

        assert(ha->pUserData != NULL);
        assert(ha->pUserData->dwHeaderOffs == ByteCount.LowPart);
        nError = CopyNonMpqData(ha->hFile, hNewFile, ByteOffset, ByteCount);
    }

    // Write the MPQ header
    if(nError == ERROR_SUCCESS)
    {
        BSWAP_TMPQHEADER(ha->pHeader);
        WriteFile(hNewFile, ha->pHeader, ha->pHeader->dwHeaderSize, &dwTransferred, NULL);
        BSWAP_TMPQHEADER(ha->pHeader);
        if(dwTransferred != ha->pHeader->dwHeaderSize)
            nError = ERROR_DISK_FULL;

        // Update the progress
        CompactBytesProcessed.QuadPart += dwTransferred;
        ha->dwFlags &= ~MPQ_FLAG_NO_HEADER;
    }

    // Now copy all files
    if(nError == ERROR_SUCCESS)
    {
        nError = CopyMpqFiles(ha, pFileKeys, hNewFile);
    }

    // If succeeded, update the tables in the file
    if(nError == ERROR_SUCCESS)
    {
        //
        // Note: We don't recalculate position of the MPQ tables at this point.
        // SaveMPQTables does it automatically.
        // 

        CloseHandle(ha->hFile);
        ha->hFile = hNewFile;
        hNewFile = INVALID_HANDLE_VALUE;
        nError = SaveMPQTables(ha);
    }

    // If all succeeded, switch the archives
    if(nError == ERROR_SUCCESS)
    {
        if(CompactCB != NULL)
        {
            CompactBytesProcessed.QuadPart += (ha->pHeader->dwHashTableSize * sizeof(TMPQHash));
            CompactBytesProcessed.QuadPart += (ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock));
            CompactCB(pvUserData, CCB_CLOSING_ARCHIVE, &CompactBytesProcessed, &CompactTotalBytes);
        }

        if(!DeleteFile(ha->szFileName) ||           // Delete the old archive
           !CloseHandle(ha->hFile)     ||           // Close the new archive
           !MoveFile(szTempFile, ha->szFileName))   // Rename the temporary archive
            nError = GetLastError();
    }

    // Now open the freshly renamed archive file
    if(nError == ERROR_SUCCESS)
    {
        ha->hFile = CreateFile(ha->szFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, MPQ_OPEN_EXISTING, 0, NULL);
        if(ha->hFile == INVALID_HANDLE_VALUE)
            nError = GetLastError();
    }

    // Invalidate the compact callback
    pvUserData = NULL;
    CompactCB = NULL;

    // Cleanup and return
    if(hNewFile != INVALID_HANDLE_VALUE)
        CloseHandle(hNewFile);
    DeleteFile(szTempFile);
    if(pFileKeys != NULL)
        FREEMEM(pFileKeys);
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// Changing hash table size

BOOL WINAPI SFileSetHashTableSize(HANDLE hMpq, DWORD dwNewTableSize)
{
    SFILE_FIND_DATA sf;
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TFileNode ** pListFileToFree = NULL;
    TFileNode ** pNewListFile = NULL;
    TMPQHash * pOldHashTable = NULL;
    TMPQHash * pNewHashTable = NULL;
    TMPQHash * pTableToFree = NULL;
    TMPQHash * pNewHash = NULL;
    HANDLE hFind;
    DWORD dwOldTableSize = ha->pHeader->dwHashTableSize;
    DWORD dwIndex;
    BOOL bFoundSomething = TRUE;
    int nError = ERROR_SUCCESS;

    // Test the valid parameters
    if(IsValidMpqHandle(ha) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
        nError = ERROR_ACCESS_DENIED;
    
    // New hash table size must be a power of two
    if(dwNewTableSize & (dwNewTableSize - 1))
        nError = ERROR_INVALID_PARAMETER;

    // Allocate buffers for copy of the old hash table and new hash table
    if(nError == ERROR_SUCCESS)
    {
        pOldHashTable = ALLOCMEM(TMPQHash, dwOldTableSize);
        pNewHashTable = ALLOCMEM(TMPQHash, dwNewTableSize);
        pNewListFile = ALLOCMEM(TFileNode *, dwNewTableSize);
        if(pOldHashTable == NULL || pNewHashTable == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // If the new hash table size is greater than maximum entries in the block table,
    // we have to reallocate block table, and also all tables in the (attributes)
    if(nError == ERROR_SUCCESS && dwNewTableSize > ha->dwBlockTableMax)
    {
        nError = ReallocateBlockTable(ha, dwNewTableSize);
    }

    // Now search for all files, and copy the found ones from old table to new table
    if(nError == ERROR_SUCCESS)
    {
        // Copy the old table and initialize new one
        memcpy(pOldHashTable, ha->pHashTable, (dwOldTableSize * sizeof(TMPQHash)));
        memset(pNewHashTable, 0xFF, (dwNewTableSize * sizeof(TMPQHash)));
        memset(pNewListFile, 0, (dwNewTableSize * sizeof(TFileNode *)));

        // Perform the file search
        hFind = SFileFindFirstFile(hMpq, "*", &sf, NULL);
        if(hFind != NULL)
        {
            while(bFoundSomething)
            {
                // Calculate the new hash table index
                pNewHash = FindFreeHashEntry(pNewHashTable, dwNewTableSize, sf.cFileName);
                if(pNewHash == NULL)
                {
                    nError = ERROR_DISK_FULL;
                    break;
                }

                // Fill the new entry. We only need to fill-in the locale and block index
                pNewHash->dwBlockIndex = sf.dwBlockIndex;
                pNewHash->lcLocale = (USHORT)sf.lcLocale;

                // Invalidate the old hash table entry
                memset(&pOldHashTable[sf.dwHashIndex], 0xFF, sizeof(TMPQHash));

                // Copy the file node
                pNewListFile[pNewHash - pNewHashTable] = ha->pListFile[sf.dwHashIndex];

                // Find next file
                bFoundSomething = SFileFindNextFile(hFind, &sf);
            }
            SFileFindClose(hFind);
        }
    }

    // Verify if we found all files, i.e. the old hash table is empty now
    if(nError == ERROR_SUCCESS)
    {
        for(dwIndex = 0; dwIndex < dwOldTableSize; dwIndex++)
        {
            // Is that entry in the old table still valid ?
            if(pOldHashTable[dwIndex].dwBlockIndex < HASH_ENTRY_DELETED)
            {
                nError = ERROR_CAN_NOT_COMPLETE;
                break;
            }
        }
    }

    // If the verification passed, we switch the tables
    if(nError == ERROR_SUCCESS)
    {
        // Backup the old hash table
        pTableToFree = ha->pHashTable;

        // Set new hash table and its size
        ha->pHeader->dwHashTableSize = dwNewTableSize;
        ha->pHashTable = pNewHashTable;
        pNewHashTable = NULL;

        // Set the new listfile
        pListFileToFree = ha->pListFile;
        ha->pListFile = pNewListFile;
        pNewListFile = NULL;

        // Mark the archive as changed
        ha->dwFlags |= MPQ_FLAG_CHANGED;
    }

    // Free buffers
    if(pListFileToFree != NULL)
        FREEMEM(pListFileToFree);
    if(pNewListFile != NULL)
        FREEMEM(pNewListFile);
    if(pOldHashTable != NULL)
        FREEMEM(pOldHashTable);
    if(pNewHashTable != NULL)
        FREEMEM(pNewHashTable);
    if(pTableToFree != NULL)
        FREEMEM(pTableToFree);

    // Return the result
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}
