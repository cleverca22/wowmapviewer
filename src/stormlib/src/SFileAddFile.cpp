/*****************************************************************************/
/* SFileAddFile.cpp                       Copyright (c) Ladislav Zezula 2010 */
/*---------------------------------------------------------------------------*/
/* MPQ Editing functions                                                     */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 27.03.10  1.00  Lad  Splitted from SFileCreateArchiveEx.cpp               */
/*****************************************************************************/

#define __STORMLIB_SELF__
#define __INCLUDE_COMPRESSION__
#define __INCLUDE_CRYPTOGRAPHY__
#include "StormLib.h"
#include "SCommon.h"

//-----------------------------------------------------------------------------
// Local variables

// Data compression for SFileAddFile
// Kept here for compatibility with code that was created with StormLib version < 6.50
static DWORD DefaultDataCompression = MPQ_COMPRESSION_PKWARE;

static SFILE_ADDFILE_CALLBACK AddFileCB = NULL;
static void * pvUserData = NULL;

//-----------------------------------------------------------------------------
// MPQ write data functions

#define LOSSY_COMPRESSION_MASK (MPQ_COMPRESSION_ADPCM_MONO | MPQ_COMPRESSION_ADPCM_STEREO | MPQ_COMPRESSION_HUFFMANN)

static int WriteDataToMpqFile(
    TMPQArchive * ha,
    TMPQFile * hf,
    LPBYTE pbFileData,
    DWORD dwDataSize,
    DWORD dwCompression)
{
    LARGE_INTEGER ByteOffset;
    TMPQBlock * pBlock = hf->pBlock;
    BYTE * pbCompressed = NULL;         // Compressed (target) data
    BYTE * pbToWrite = NULL;            // Data to write to the file
    DWORD dwTransferred = 0;            // Number of bytes read or written
    int nCompressionLevel = -1;         // ADPCM compression level (only used for wave files)
    int nError = ERROR_SUCCESS;

    // If the caller wants ADPCM compression, we will set wave compression level to 4,
    // which corresponds to medium quality
    if(dwCompression & LOSSY_COMPRESSION_MASK)
        nCompressionLevel = 4;

    // Make sure that the caller won't overrun the previously initiated file size
    assert(hf->dwFilePos + dwDataSize <= pBlock->dwFSize);
    assert(hf->dwSectorCount != 0);
    assert(hf->pbFileSector != NULL);
    pbToWrite = hf->pbFileSector;

    // If we are going to write the data, set the file position and
    // also allocate compression buffer
    if((hf->dwFilePos + dwDataSize) >= hf->dwSectorSize || (hf->dwFilePos + dwDataSize) >= pBlock->dwFSize)
    {
        // Set the position in the file
        ByteOffset.QuadPart = hf->RawFilePos.QuadPart;
        ByteOffset.QuadPart += pBlock->dwCSize;
        SetFilePointer(ha->hFile, ByteOffset.LowPart, (PLONG)&ByteOffset.HighPart, FILE_BEGIN);

        // If the file is compressed, allocate buffer for the compressed data.
        // Note that we allocate buffer that is a bit longer than sector size,
        // for case if the compression method performs a buffer overrun
        if(pBlock->dwFlags & MPQ_FILE_COMPRESSED)
        {
            pbToWrite = pbCompressed = ALLOCMEM(BYTE, hf->dwSectorSize + 0x100);
            if(pbCompressed == NULL)
                nError = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    // Now write all data to the file sector buffer
    if(nError == ERROR_SUCCESS)
    {
        DWORD dwBytesInSector = hf->dwFilePos % hf->dwSectorSize;
        DWORD dwSectorIndex = hf->dwFilePos / hf->dwSectorSize;
        DWORD dwBytesToCopy;

        // Process all data. 
        while(dwDataSize != 0)
        {
            dwBytesToCopy = dwDataSize;
                
            // Check for sector overflow
            if(dwBytesToCopy > (hf->dwSectorSize - dwBytesInSector))
                dwBytesToCopy = (hf->dwSectorSize - dwBytesInSector);

            // Copy the data to the file sector
            memcpy(hf->pbFileSector + dwBytesInSector, pbFileData, dwBytesToCopy);
            dwBytesInSector += dwBytesToCopy;
            pbFileData += dwBytesToCopy;
            dwDataSize -= dwBytesToCopy;

            // Update the file position
            hf->dwFilePos += dwBytesToCopy;

            // If the current sector is full, or if the file is already full,
            // then write the data to the MPQ
            if(dwBytesInSector >= hf->dwSectorSize || hf->dwFilePos >= pBlock->dwFSize)
            {
                // Update CRC32 and MD5 of the file
                md5_process((hash_state *)hf->hctx, hf->pbFileSector, dwBytesInSector);
                crc32(hf->dwCrc32, hf->pbFileSector, dwBytesInSector);

                // Compress the file sector, if needed
                if(pBlock->dwFlags & MPQ_FILE_COMPRESSED)
                {
                    int nOutBuffer = (int)dwBytesInSector;
                    int nInBuffer = (int)dwBytesInSector;

                    assert(pbCompressed != NULL);

                    //
                    // Note that both SCompImplode and SCompCompress give original buffer,
                    // if they are unable to comperss the data.
                    //

                    if(pBlock->dwFlags & MPQ_FILE_IMPLODE)
                    {
                        SCompImplode((char *)pbCompressed,
                                            &nOutBuffer,
                                     (char *)hf->pbFileSector,
                                             nInBuffer);
                    }

                    if(pBlock->dwFlags & MPQ_FILE_COMPRESS)
                    {
                        SCompCompress((char *)pbCompressed,
                                             &nOutBuffer,
                                      (char *)hf->pbFileSector,
                                              nInBuffer,
                                    (unsigned)dwCompression,
                                              0,
                                              nCompressionLevel);
                    }

                    // Update sector positions
                    dwBytesInSector = nOutBuffer;
                    if(hf->SectorOffsets != NULL)
                        hf->SectorOffsets[dwSectorIndex+1] = hf->SectorOffsets[dwSectorIndex] + dwBytesInSector;

                    // We have to calculate sector CRC, if enabled
                    if(hf->SectorChksums != NULL)
                        hf->SectorChksums[dwSectorIndex] = adler32(0, pbCompressed, nOutBuffer);
                }                 

                // Encrypt the sector, if necessary
                if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
                {
                    BSWAP_ARRAY32_UNSIGNED(pbToWrite, dwBytesInSector);
                    EncryptMpqBlock(pbToWrite, dwBytesInSector, hf->dwFileKey + dwSectorIndex);
                    BSWAP_ARRAY32_UNSIGNED(pbToWrite, dwBytesInSector);
                }

                // Write the file sector
                WriteFile(ha->hFile, pbToWrite, dwBytesInSector, &dwTransferred, NULL);
                if(dwTransferred != dwBytesInSector)
                {
                    nError = ERROR_DISK_FULL;
                    break;
                }

                // Call the compact callback, if any
                if(AddFileCB != NULL)
                    AddFileCB(pvUserData, hf->dwFilePos, pBlock->dwFSize, FALSE);

                // Update the compressed file size
                pBlock->dwCSize += dwTransferred;
                dwBytesInSector = 0;
                dwSectorIndex++;
            }
        }
    }

    // Cleanup
    if(pbCompressed != NULL)
        FREEMEM(pbCompressed);
    return nError;
}

//-----------------------------------------------------------------------------
// Recrypts file data for file renaming

static int RecryptFileData(
    TMPQArchive * ha,
    TMPQFile * hf,
    const char * szFileName,
    const char * szNewFileName)
{
    LARGE_INTEGER RawFilePos;
    TMPQBlockEx * pBlockEx = hf->pBlockEx;
    TMPQBlock * pBlock = hf->pBlock;
    DWORD dwBytesToRecrypt = pBlock->dwCSize;
    DWORD dwTransferred;
    DWORD dwOldKey;
    DWORD dwNewKey;
    int nError = ERROR_SUCCESS;

    // The file must be encrypted
    assert(pBlock->dwFlags & MPQ_FILE_ENCRYPTED);

    // File decryption key is calculated from the plain name
    szNewFileName = GetPlainMpqFileName(szNewFileName);
    szFileName = GetPlainMpqFileName(szFileName);

    // Calculate both file keys
    dwOldKey = DecryptFileKey(szFileName);
    dwNewKey = DecryptFileKey(szNewFileName);
    if(pBlock->dwFlags & MPQ_FILE_FIX_KEY)
    {
        dwOldKey = (dwOldKey + pBlock->dwFilePos) ^ pBlock->dwFSize;
        dwNewKey = (dwNewKey + pBlock->dwFilePos) ^ pBlock->dwFSize;
    }

    // Incase the keys are equal, don't recrypt the file
    if(dwNewKey == dwOldKey)
        return ERROR_SUCCESS;
    hf->dwFileKey = dwOldKey;

    // Calculate the raw position of the file in the archive
    hf->MpqFilePos.LowPart = pBlock->dwFilePos;
    hf->MpqFilePos.HighPart = pBlockEx->wFilePosHigh;
    hf->RawFilePos.QuadPart = ha->MpqPos.QuadPart + hf->MpqFilePos.QuadPart;
    SetFilePointer(ha->hFile, hf->RawFilePos.LowPart, &hf->RawFilePos.HighPart, FILE_BEGIN);

    // Allocate buffer for file transfer
    nError = AllocateSectorBuffer(hf);
    if(nError != ERROR_SUCCESS)
        return nError;

    // Also allocate buffer for sector offsets
    // Note: Don't load sector checksums, we don't need to recrypt them
    nError = AllocateSectorOffsets(hf, true);
    if(nError != ERROR_SUCCESS)
        return nError;

    // If we have sector offsets, recrypt these as well
    if(hf->SectorOffsets != NULL)
    {
        // Allocate secondary buffer for sectors copy
        DWORD * SectorOffsetsCopy = ALLOCMEM(DWORD, hf->dwSectorCount);
        DWORD dwArraySize = hf->dwSectorCount * sizeof(DWORD);

        if(SectorOffsetsCopy == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Recrypt the array of sector offsets
        memcpy(SectorOffsetsCopy, hf->SectorOffsets, dwArraySize);
        EncryptMpqBlock(SectorOffsetsCopy, dwArraySize, dwNewKey - 1);
        BSWAP_ARRAY32_UNSIGNED(SectorOffsetsCopy, dwArraySize);

        // Write the recrypted array back
        SetFilePointer(ha->hFile, hf->RawFilePos.LowPart, &hf->RawFilePos.HighPart, FILE_BEGIN);
        WriteFile(ha->hFile, SectorOffsetsCopy, dwArraySize, &dwTransferred, NULL);
        FREEMEM(SectorOffsetsCopy);

        if(dwTransferred != dwArraySize)
            nError = ERROR_WRITE_FAULT;
    }

    // Now we have to recrypt all file sectors. We do it without
    // recompression, because recompression is not necessary in this case
    if(nError == ERROR_SUCCESS)
    {
        for(DWORD dwSector = 0; dwSector < hf->dwDataSectors; dwSector++)
        {
            DWORD dwRawDataInSector = hf->dwSectorSize;
            DWORD dwRawByteOffset = dwSector * hf->dwSectorSize;

            // Last sector: If there is not enough bytes remaining in the file, cut the raw size
            if(dwRawDataInSector > dwBytesToRecrypt)
                dwRawDataInSector = dwBytesToRecrypt;

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
            BSWAP_ARRAY32_UNSIGNED(hf->pbFileSector, dwRawDataInSector);
            DecryptMpqBlock(hf->pbFileSector, dwRawDataInSector, dwOldKey + dwSector);
            EncryptMpqBlock(hf->pbFileSector, dwRawDataInSector, dwNewKey + dwSector);
            BSWAP_ARRAY32_UNSIGNED(hf->pbFileSector, dwRawDataInSector);

            // Write the sector back
            SetFilePointer(ha->hFile, RawFilePos.LowPart, &RawFilePos.HighPart, FILE_BEGIN);
            WriteFile(ha->hFile, hf->pbFileSector, dwRawDataInSector, &dwTransferred, NULL);
            if(dwTransferred != dwRawDataInSector)
            {
                nError = ERROR_FILE_CORRUPT;
                break;
            }

            // Decrement number of bytes remaining
            dwBytesToRecrypt -= hf->dwSectorSize;
        }
    }

    return nError;
}

//-----------------------------------------------------------------------------
// Support functions for adding files to the MPQ

int SFileAddFile_Init(
    TMPQArchive * ha,
    const char * szArchivedName,
    TMPQFileTime * pFT,
    DWORD dwFileSize,
    LCID lcLocale,
    DWORD dwFlags,
    void ** ppvAddHandle)
{
    LARGE_INTEGER TempPos;              // For various file offset calculations
    TMPQFileTime * pFileTime;
    TMPQFile * hf = NULL;               // File structure for newly added file
    TMPQMD5 * pMd5;
    DWORD * pCrc32;
    int nError = ERROR_SUCCESS;

    //
    // Note: This is an internal function so no validity checks are done.
    // It is the caller's responsibility to make sure that no invalid
    // flags get to this point
    //

    // Adjust file flags for too-small files
    if(dwFileSize < 0x04)
        dwFlags &= ~(MPQ_FILE_ENCRYPTED | MPQ_FILE_FIX_KEY);
    if(dwFileSize < 0x20)
        dwFlags &= ~(MPQ_FILE_COMPRESSED | MPQ_FILE_SECTOR_CRC);

    // Allocate the TMPQFile entry for newly added file
    hf = CreateMpqFile(ha, szArchivedName);
    if(hf == NULL)
        nError = ERROR_NOT_ENOUGH_MEMORY;

    // If the MPQ header has not yet been written, do it now
    if(nError == ERROR_SUCCESS && (ha->dwFlags & MPQ_FLAG_NO_HEADER))
    {
        DWORD dwTransferred;

        BSWAP_TMPQHEADER(ha->pHeader);
        WriteFile(ha->hFile, ha->pHeader, ha->pHeader->dwHeaderSize, &dwTransferred, NULL);
        BSWAP_TMPQHEADER(ha->pHeader);

        if(dwTransferred == ha->pHeader->dwHeaderSize)
            ha->dwFlags &= ~MPQ_FLAG_NO_HEADER;
        else
            nError = ERROR_DISK_FULL;
    }

    if(nError == ERROR_SUCCESS)
    {
        // Check if the file already exists in the archive
        if((hf->pHash = GetHashEntryExact(ha, szArchivedName, lcLocale)) != NULL)
        {
            if(dwFlags & MPQ_FILE_REPLACEEXISTING)
            {
                hf->pBlockEx = ha->pExtBlockTable + hf->pHash->dwBlockIndex;
                hf->pBlock = ha->pBlockTable + hf->pHash->dwBlockIndex;
            }
            else
            {
                nError = ERROR_ALREADY_EXISTS;
                hf->pHash = NULL;
            }
        }

        if(nError == ERROR_SUCCESS && hf->pHash == NULL)
        {
            hf->pHash = FindFreeHashEntry(ha, szArchivedName);
            if(hf->pHash == NULL)
                nError = ERROR_HANDLE_DISK_FULL;
        }

        // Set the hash index
        hf->dwHashIndex = (DWORD)(hf->pHash - ha->pHashTable);
    }

    // Find a free space in the MPQ, as well as free block table entry
    if(nError == ERROR_SUCCESS)
    {
        DWORD dwFreeBlock = FindFreeMpqSpace(ha, &hf->MpqFilePos);

        // Calculate the raw file offset
        hf->RawFilePos.QuadPart = ha->MpqPos.QuadPart + hf->MpqFilePos.QuadPart;

        // When format V1, the size of the archive cannot exceed 4 GB
        if(ha->pHeader->wFormatVersion == MPQ_FORMAT_VERSION_1)
        {
            TempPos.QuadPart  = hf->MpqFilePos.QuadPart + dwFileSize;
            TempPos.QuadPart += ha->pHeader->dwHashTableSize * sizeof(TMPQHash);
            TempPos.QuadPart += ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock);
            TempPos.QuadPart += ha->pHeader->dwBlockTableSize * sizeof(TMPQBlockEx);
            if(TempPos.HighPart != 0)
                nError = ERROR_DISK_FULL;
        }

        // If we didn't get a block table entry assigned from hash table, assign it now
        if(hf->pBlock == NULL)
        {
            // Note: dwFreeBlock can be greater than dwHashTableSize,
            // in case that block table is bigger than hash table
            if(dwFreeBlock != 0xFFFFFFFF)
            {
                hf->pBlockEx = ha->pExtBlockTable + dwFreeBlock;
                hf->pBlock = ha->pBlockTable + dwFreeBlock;
            }
            else
            {
                nError = ERROR_HANDLE_DISK_FULL;
            }
        }

        // Calculate the index to the block table
        hf->dwBlockIndex = (DWORD)(hf->pBlock - ha->pBlockTable);
    }

    // Create key for file encryption
    if(nError == ERROR_SUCCESS && (dwFlags & MPQ_FILE_ENCRYPTED))
    {
        szArchivedName = GetPlainMpqFileName(szArchivedName);
        hf->dwFileKey = DecryptFileKey(szArchivedName);
        if(dwFlags & MPQ_FILE_FIX_KEY)
            hf->dwFileKey = (hf->dwFileKey + hf->MpqFilePos.LowPart) ^ dwFileSize;
    }

    if(nError == ERROR_SUCCESS)
    {
        // Initialize the hash entry for the file
        hf->pHash->dwBlockIndex = hf->dwBlockIndex;
        hf->pHash->lcLocale = (USHORT)lcLocale;

        // Initialize the block table entry for the file
        hf->pBlockEx->wFilePosHigh = (USHORT)hf->MpqFilePos.HighPart;
        hf->pBlock->dwFilePos = hf->MpqFilePos.LowPart;
        hf->pBlock->dwFSize = dwFileSize;
        hf->pBlock->dwCSize = 0;
        hf->pBlock->dwFlags = dwFlags | MPQ_FILE_EXISTS;

        // Resolve CRC32 and MD5 entry for the file
        // Only do it when the MPQ archive has attributes
        if(ha->pAttributes != NULL)
        {
            pFileTime = ha->pAttributes->pFileTime + hf->dwBlockIndex;
            pCrc32 = ha->pAttributes->pCrc32 + hf->dwBlockIndex;
            pMd5 = ha->pAttributes->pMd5 + hf->dwBlockIndex;

            // If the file has been overwritten, there still might be
            // stale entries in the attributes
            memset(pFileTime, 0, sizeof(TMPQFileTime));
            memset(pMd5, 0, sizeof(TMPQMD5));
            pCrc32[0] = 0;

            // Only use FILETIME, CRC32, and MD5 if the caller gave us
            // a valid FILETIME pointer
            if(pFT != NULL)
            {
                assert(sizeof(hf->hctx) >= sizeof(hash_state));
                hf->pFileTime = pFileTime;
                hf->pCrc32 = pCrc32;
                hf->pMd5 = pMd5;

                // Initialize the file's FILETIME, CRC32 and MD5
                md5_init((hash_state *)hf->hctx);
                hf->dwCrc32 = crc32(0, Z_NULL, 0);
                *hf->pFileTime = *pFT;
            }
        }

        // Call the callback, if needed
        if(AddFileCB != NULL)
            AddFileCB(pvUserData, 0, hf->pBlock->dwFSize, FALSE);
    }

    *ppvAddHandle = hf;
    return nError;
}

int SFileAddFile_Write(void * pvAddHandle, void * pvData, DWORD dwSize, DWORD dwCompression)
{
    TMPQArchive * ha;
    TMPQBlock * pBlock;
    TMPQFile * hf = (TMPQFile *)pvAddHandle;
    DWORD dwSectorPosLen = 0;
    DWORD dwTransferred = 0;            // Number of bytes read or written
    int nError = ERROR_SUCCESS;

    // Don't bother if the caller gave us zero size
    if(pvData == NULL || dwSize == 0)
        return ERROR_SUCCESS;

    // Get pointer to the MPQ archive
    pBlock = hf->pBlock;
    ha = hf->ha;

    // Allocate file buffers
    if(hf->pbFileSector == NULL)
    {
        nError = AllocateSectorBuffer(hf);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Allocate sector offsets
        if(hf->SectorOffsets == NULL)
        {
            nError = AllocateSectorOffsets(hf, false);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // Create array of sector checksums
        if(hf->SectorChksums == NULL && (pBlock->dwFlags & MPQ_FILE_SECTOR_CRC))
        {
            nError = AllocateSectorChecksums(hf, false);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // Pre-save the sector offset table, just to reserve space in the file.
        // Note that we dont need to swap the sector positions, nor encrypt the table
        // at the moment, as it will be written again after writing all file sectors.
        if(hf->SectorOffsets != NULL)
        {
            SetFilePointer(ha->hFile, hf->RawFilePos.LowPart, (PLONG)&hf->RawFilePos.HighPart, FILE_BEGIN);
            dwSectorPosLen = hf->dwSectorCount * sizeof(DWORD);
            WriteFile(ha->hFile, hf->SectorOffsets, dwSectorPosLen, &dwTransferred, NULL);
            if(dwTransferred != dwSectorPosLen)
                nError = ERROR_DISK_FULL;
            pBlock->dwCSize += dwSectorPosLen;
        }
    }

    // Write the MPQ data to the file
    if(nError == ERROR_SUCCESS)
        nError = WriteDataToMpqFile(ha, hf, (LPBYTE)pvData, dwSize, dwCompression);

    // If it succeeded and we wrote all the file data,
    // we need to re-save sector offset table
    if((nError == ERROR_SUCCESS) && (hf->dwFilePos >= pBlock->dwFSize))
    {
        // Finish calculating MD5
        if(hf->pMd5 != NULL)
            md5_done((hash_state *)hf->hctx, hf->pMd5->Value);

        // If we also have sector checksums, write them to the file
        if(hf->SectorChksums != NULL)
            WriteSectorChecksums(hf);

        // Now write sector offsets to the file
        if(hf->SectorOffsets != NULL)
            WriteSectorOffsets(hf);
    }

    return nError;
}

int SFileAddFile_Finish(void * pvAddHandle, int nError)
{
    TMPQArchive * ha;
    TMPQFile * hf = (TMPQFile *)pvAddHandle;

    // Get pointer to the MPQ archive
    ha = hf->ha;

    // If all previous operations succeeded, we can update the MPQ
    if(nError == ERROR_SUCCESS)
    {
        // Increment the block table size, if needed
        if(hf->dwBlockIndex >= ha->pHeader->dwBlockTableSize)
            ha->pHeader->dwBlockTableSize++;

        // Add the file into listfile also
        // Don't bother checking result, it either succeeds or not.
        SListFileCreateNode(ha, hf->szFileName, hf->pHash);

        //
        // Note: Don't recalculate position of MPQ tables at this point.
        // We merely set the flag that indicates that the MPQ tables
        // have been changed, and SaveMpqTables will do the work when closing the archive.
        //

        // Call the user callback, if any
        if(AddFileCB != NULL)
            AddFileCB(pvUserData, hf->pBlock->dwFSize, hf->pBlock->dwFSize, TRUE);
        ha->dwFlags |= MPQ_FLAG_CHANGED;
    }
    else
    {
        if(hf != NULL)
        {
            // Clear the hash table entry and block table entry
            if(hf->pHash != NULL)
                memset(hf->pHash, 0xFF, sizeof(TMPQHash));
            if(hf->pBlock != NULL)
                memset(hf->pBlock, 0, sizeof(TMPQBlock));
            if(hf->pBlockEx != NULL)
                hf->pBlockEx->wFilePosHigh = 0;
        }
    }

    // Clear the add file callback
    FreeMPQFile(hf);
    pvUserData = NULL;
    AddFileCB = NULL;
    return FALSE;
}

//-----------------------------------------------------------------------------
// Adds a file into the archive

BOOL WINAPI SFileAddFileEx(
    HANDLE hMpq,
    const char * szFileName,
    const char * szArchivedName,
    DWORD dwFlags,
    DWORD dwCompression,            // Compression of the first sector
    DWORD dwCompressionNext)        // Compression of next sectors
{
    TMPQFileTime ft = {0, 0};
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    LPBYTE pbFileData = NULL;
    VOID * pvAddHandle = NULL;
    DWORD dwFileSizeHigh = 0;
    DWORD dwTransferred;
    DWORD dwSectorSize = 0x1000;
    DWORD dwFileSize = 0;
    int nError = ERROR_SUCCESS;

    // Check valid parameters
    if(IsValidMpqHandle(ha) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(szFileName == NULL || *szFileName == 0 || szArchivedName == NULL || *szArchivedName == 0)
        nError = ERROR_INVALID_PARAMETER;
    
    // Don't allow to add file if the MPQ is open for read only
    if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
        nError = ERROR_ACCESS_DENIED;
    
    // Don't allow to add any of the internal files
    if(!_stricmp(szArchivedName, LISTFILE_NAME))
        nError = ERROR_ACCESS_DENIED;
    if(!_stricmp(szArchivedName, ATTRIBUTES_NAME))
        nError = ERROR_ACCESS_DENIED;
    if(!_stricmp(szArchivedName, SIGNATURE_NAME))
        nError = ERROR_ACCESS_DENIED;

    // Perform validity check of the MPQ flags
    if(nError == ERROR_SUCCESS)
    {
        // Mask all unsupported flags out
        dwFlags &= MPQ_FILE_VALID_FLAGS;

        // Check for valid flag combinations
        if((dwFlags & MPQ_FILE_IMPLODE) && (dwFlags & MPQ_FILE_COMPRESS))
            nError = ERROR_INVALID_PARAMETER;

        // Special checks for single unit files
        if(dwFlags & MPQ_FILE_SINGLE_UNIT)
        {
            //
            // Note: Blizzard doesn't support single unit files
            // that are stored as encrypted or imploded. We will allow them here,
            // the calling application must ensure that such flag combination doesn't get here
            //

//          if(dwFlags & MPQ_FILE_IMPLODE)
//              nError = ERROR_INVALID_PARAMETER;
//
//          if(dwFlags & MPQ_FILE_ENCRYPTED)
//              nError = ERROR_INVALID_PARAMETER;
            
            // Lossy compression is not allowed on single unit files
            if(dwCompression & LOSSY_COMPRESSION_MASK)
                nError = ERROR_INVALID_PARAMETER;
        }
    }

    // Open added file
    if(nError == ERROR_SUCCESS)
    {
        hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, 0, MPQ_OPEN_EXISTING, 0, NULL);
        if(hFile == INVALID_HANDLE_VALUE)
            nError = GetLastError();
    }

    // Get the file size and file time
    if(nError == ERROR_SUCCESS)
    {
        GetFileTime(hFile, NULL, NULL, (LPFILETIME)&ft);

        dwFileSize = GetFileSize(hFile, &dwFileSizeHigh);
        if(dwFileSizeHigh != 0)
            nError = ERROR_PARAMETER_QUOTA_EXCEEDED;
    }

    // Allocate data buffer for reading from the source file
    if(nError == ERROR_SUCCESS)
    {
        pbFileData = ALLOCMEM(BYTE, dwSectorSize);
        if(pbFileData == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Deal with various combination of compressions
    if(nError == ERROR_SUCCESS)
    {
        // When the compression for next blocks is set to default,
        // we will copy the compression for the first sector
        if(dwCompressionNext == 0xFFFFFFFF)
            dwCompressionNext = dwCompression;
        
        // If the caller wants ADPCM compression, we make sure that the first sector is not
        // compressed with lossy compression
        if(dwCompressionNext & (MPQ_COMPRESSION_WAVE_MONO | MPQ_COMPRESSION_WAVE_STEREO))
        {
            // The first compression must not be WAVE
            if(dwCompression & (MPQ_COMPRESSION_WAVE_MONO | MPQ_COMPRESSION_WAVE_STEREO))
                dwCompression = MPQ_COMPRESSION_PKWARE;
        }

        // Initiate adding file to the MPQ
        nError = SFileAddFile_Init(ha, szArchivedName, &ft, dwFileSize, lcFileLocale, dwFlags, &pvAddHandle);
        while(nError == ERROR_SUCCESS)
        {
            // Read data from the local file
            ReadFile(hFile, pbFileData, dwSectorSize, &dwTransferred, NULL);
            if(dwTransferred == 0)
                break;

            // Add the file sectors to the MPQ
            nError = SFileAddFile_Write(pvAddHandle, pbFileData, dwTransferred, dwCompression);
            if(nError != ERROR_SUCCESS)
                break;

            // Set the next data compression
            dwCompression = dwCompressionNext;
        }

        // Finalize the file
        SFileAddFile_Finish(pvAddHandle, nError);
    }

    // Cleanup and exit
    if(pbFileData != NULL)
        FREEMEM(pbFileData);
    if(hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}
                                                                                                                                 
// Adds a data file into the archive
BOOL WINAPI SFileAddFile(HANDLE hMpq, const char * szFileName, const char * szArchivedName, DWORD dwFlags)
{
    return SFileAddFileEx(hMpq,
                          szFileName,
                          szArchivedName,
                          dwFlags,
                          DefaultDataCompression,
                          DefaultDataCompression);
}

// Adds a WAVE file into the archive
BOOL WINAPI SFileAddWave(HANDLE hMpq, const char * szFileName, const char * szArchivedName, DWORD dwFlags, DWORD dwQuality)
{
    DWORD dwCompression = 0;

    //
    // Note to wave compression level:
    // This was only used by Diablo I. The following conversion table applied:
    // High quality:   WaveCompressionLevel = -1
    // Medium quality: WaveCompressionLevel = 4
    // Low quality:    WaveCompressionLevel = 2
    //
    // Diablo files are packed as Mono (0x41) on medium quality.
    // Because this compression is not used anymore, our compression functions
    // will default to WaveCompressionLevel = 4 when using ADPCM compression
    // 

    // Convert quality to data compression
    switch(dwQuality)
    {
        case MPQ_WAVE_QUALITY_HIGH:
//          WaveCompressionLevel = -1;
            dwCompression = MPQ_COMPRESSION_PKWARE;
            break;

        case MPQ_WAVE_QUALITY_MEDIUM:
//          WaveCompressionLevel = 4;
            dwCompression = MPQ_COMPRESSION_WAVE_STEREO | MPQ_COMPRESSION_HUFFMANN;
            break;

        case MPQ_WAVE_QUALITY_LOW:
//          WaveCompressionLevel = 2;
            dwCompression = MPQ_COMPRESSION_WAVE_STEREO | MPQ_COMPRESSION_HUFFMANN;
            break;
    }

    return SFileAddFileEx(hMpq,
                          szFileName,
                          szArchivedName,
                          dwFlags,
                          MPQ_COMPRESSION_PKWARE,   // First sector should be compressed as data
                          dwCompression);           // Next sectors should be compressed as WAVE
}

//-----------------------------------------------------------------------------
// BOOL SFileRemoveFile(HANDLE hMpq, char * szFileName)
//
// This function removes a file from the archive. The file content
// remains there, only the entries in the hash table and in the block
// table are updated. 

BOOL WINAPI SFileRemoveFile(HANDLE hMpq, const char * szFileName, DWORD dwSearchScope)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TMPQBlockEx * pBlockEx = NULL;  // Block entry of deleted file
    TMPQBlock   * pBlock = NULL;    // Block entry of deleted file
    TMPQHash    * pHash = NULL;     // Hash entry of deleted file
    DWORD dwBlockIndex = 0;
    int nError = ERROR_SUCCESS;

    // Check the parameters
    if(nError == ERROR_SUCCESS)
    {
        if(IsValidMpqHandle(ha) == FALSE)
            nError = ERROR_INVALID_HANDLE;
        if(dwSearchScope != SFILE_OPEN_BY_INDEX && *szFileName == 0)
            nError = ERROR_INVALID_PARAMETER;
    }

    if(nError == ERROR_SUCCESS)
    {
        // Do not allow to remove files from MPQ open for read only
        if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
            nError = ERROR_ACCESS_DENIED;

        // Do not allow to remove internal files
        if(dwSearchScope != SFILE_OPEN_BY_INDEX)
        {
            if(!_stricmp(szFileName, LISTFILE_NAME))
                nError = ERROR_ACCESS_DENIED;
            if(!_stricmp(szFileName, ATTRIBUTES_NAME))
                nError = ERROR_ACCESS_DENIED;
            if(!_stricmp(szFileName, SIGNATURE_NAME))
                nError = ERROR_ACCESS_DENIED;
        }
    }

    // Get hash entry belonging to this file
    if(nError == ERROR_SUCCESS)
    {
        if(dwSearchScope == SFILE_OPEN_FROM_MPQ)
        {
            if((pHash = GetHashEntryExact(ha, (char *)szFileName, lcFileLocale)) == NULL)
                nError = ERROR_FILE_NOT_FOUND;
        }
        else
        {
            if((pHash = GetHashEntryByIndex(ha, (DWORD)(DWORD_PTR)szFileName)) == NULL)
                nError = ERROR_FILE_NOT_FOUND;
        }
    }

    // If index was not found, or is greater than number of files, exit.
    if(nError == ERROR_SUCCESS)
    {
        if((dwBlockIndex = pHash->dwBlockIndex) > ha->pHeader->dwBlockTableSize)
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Get block and test if the file is not already deleted
    if(nError == ERROR_SUCCESS)
    {
        pBlockEx = ha->pExtBlockTable + dwBlockIndex;
        pBlock = ha->pBlockTable + dwBlockIndex;
        if((pBlock->dwFlags & MPQ_FILE_EXISTS) == 0)
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Remove the file from the list file
    if(nError == ERROR_SUCCESS)
        nError = SListFileRemoveNode(ha, pHash);

    if(nError == ERROR_SUCCESS)
    {
        // Now invalidate the block entry and the hash entry. Do not make any
        // relocations and file copying, use SFileCompactArchive for it.
        pBlockEx->wFilePosHigh = 0;
        pBlock->dwFilePos   = 0;
        pBlock->dwFSize     = 0;
        pBlock->dwCSize     = 0;
        pBlock->dwFlags     = 0;
        pHash->dwName1      = 0xFFFFFFFF;
        pHash->dwName2      = 0xFFFFFFFF;
        pHash->lcLocale     = 0xFFFF;
        pHash->wPlatform    = 0xFFFF;
        pHash->dwBlockIndex = HASH_ENTRY_DELETED;

        // Note: We don't decrease size of the block table,
        // even if this was the last blocktable entry.

        // Update MPQ archive
        ha->dwFlags |= MPQ_FLAG_CHANGED;
    }

    // Resolve error and exit
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

// Renames the file within the archive.
BOOL WINAPI SFileRenameFile(HANDLE hMpq, const char * szFileName, const char * szNewFileName)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TMPQBlock * pBlock;
    TMPQHash * pOldHash = NULL;         // Hash entry for the original file
    TMPQHash * pNewHash = NULL;         // Hash entry for the renamed file
    TMPQFile * hf;
    DWORD dwOldBlockIndex = 0;
    LCID lcSaveLocale = 0;
    int nError = ERROR_SUCCESS;

    // Test the valid parameters
    if(nError == ERROR_SUCCESS)
    {
        if(IsValidMpqHandle(ha) == FALSE)
            nError = ERROR_INVALID_HANDLE;
        if(szFileName == NULL || *szFileName == 0 || szNewFileName == NULL || *szNewFileName == 0)
            nError = ERROR_INVALID_PARAMETER;
    }

    if(nError == ERROR_SUCCESS)
    {
        // Do not allow to rename files in MPQ open for read only
        if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
            nError = ERROR_ACCESS_DENIED;

        // Do not allow to rename any of the internal files
        if(!_stricmp(szFileName, LISTFILE_NAME))
            nError = ERROR_ACCESS_DENIED;
        if(!_stricmp(szFileName, ATTRIBUTES_NAME))
            nError = ERROR_ACCESS_DENIED;
        if(!_stricmp(szFileName, SIGNATURE_NAME))
            nError = ERROR_ACCESS_DENIED;

        // Also do not allow to rename any of files to an internal file
        if(!_stricmp(szNewFileName, LISTFILE_NAME))
            nError = ERROR_ACCESS_DENIED;
        if(!_stricmp(szNewFileName, ATTRIBUTES_NAME))
            nError = ERROR_ACCESS_DENIED;
        if(!_stricmp(szNewFileName, SIGNATURE_NAME))
            nError = ERROR_ACCESS_DENIED;
    }

    // Get the hash table entry for the original file
    if(nError == ERROR_SUCCESS)
    {
        if((pOldHash = GetHashEntryExact(ha, szFileName, lcFileLocale)) == NULL)
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Test if the file already exists in the archive
    if(nError == ERROR_SUCCESS)
    {
        if((pNewHash = GetHashEntryExact(ha, szNewFileName, lcFileLocale)) != NULL)
            nError = ERROR_ALREADY_EXISTS;
    }

    // We have to know the decryption key, otherwise we cannot re-crypt
    // the file after renaming
    if(nError == ERROR_SUCCESS)
    {
        // Save block table index and remove the hash table entry
        dwOldBlockIndex = pOldHash->dwBlockIndex;
        lcSaveLocale = pOldHash->lcLocale;
        pBlock = ha->pBlockTable + dwOldBlockIndex;

        // If the file is encrypted, we have to re-crypt the file content
        // with the new decryption key
        if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
        {
            hf = CreateMpqFile(ha, "<renaming>");
            if(hf != NULL)
            {
                hf->pHash = pOldHash;
                hf->pBlock = ha->pBlockTable + dwOldBlockIndex;
                hf->pBlockEx = ha->pExtBlockTable + dwOldBlockIndex;
                nError = RecryptFileData(ha, hf, szFileName, szNewFileName);
                FreeMPQFile(hf);
            }
            else
            {
                nError = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
    }

    // Get the hash table entry for the renamed file
    if(nError == ERROR_SUCCESS)
    {
        SListFileRemoveNode(ha, pOldHash);
        pOldHash->dwName1      = 0xFFFFFFFF;
        pOldHash->dwName2      = 0xFFFFFFFF;
        pOldHash->lcLocale     = 0xFFFF;
        pOldHash->wPlatform    = 0xFFFF;
        pOldHash->dwBlockIndex = HASH_ENTRY_DELETED;

        // Note that this should always succeed; even if the hash table
        // was full, one entry was freed before.
        if((pNewHash = FindFreeHashEntry(ha, szNewFileName)) == NULL)
            nError = ERROR_CAN_NOT_COMPLETE;
    }

    // Save the block index and clear the hash entry
    if(nError == ERROR_SUCCESS)
    {
        // Copy the block table index
        pNewHash->dwBlockIndex = dwOldBlockIndex;
        pNewHash->lcLocale = (USHORT)lcSaveLocale;
        ha->dwFlags |= MPQ_FLAG_CHANGED;

        // Create new name node for the listfile
        nError = SListFileCreateNode(ha, szNewFileName, pNewHash);
    }

    // Resolve error and return
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// Sets default data compression for SFileAddFile

BOOL WINAPI SFileSetDataCompression(DWORD DataCompression)
{
    unsigned int uValidMask = (MPQ_COMPRESSION_ZLIB | MPQ_COMPRESSION_PKWARE | MPQ_COMPRESSION_BZIP2 | MPQ_COMPRESSION_SPARSE);

    if((DataCompression & uValidMask) != DataCompression)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    DefaultDataCompression = DataCompression;
    return TRUE;
}

//-----------------------------------------------------------------------------
// Changes locale ID of a file

BOOL WINAPI SFileSetFileLocale(HANDLE hFile, LCID lcNewLocale)
{
    TMPQArchive * ha;
    TMPQFile * hf = (TMPQFile *)hFile;
    TMPQHash * pHashEnd;
    TMPQHash * pHash;

    // Invalid handle => do nothing
    if(IsValidFileHandle(hf) == FALSE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    // Do not allow to rename files in MPQ open for read only
    ha = hf->ha;
    if(ha->dwFlags & MPQ_FLAG_READ_ONLY)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    // If the file already has that locale, return OK
    if(hf->pHash->lcLocale == lcNewLocale)
        return TRUE;

    // We have to check if the file+locale is not already there
    pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
    for(pHash = ha->pHashTable; pHash < pHashEnd; pHash++)
    {
        if(pHash->dwName1 == hf->pHash->dwName1 && pHash->dwName2 == hf->pHash->dwName2 && pHash->lcLocale == lcNewLocale)
        {
            SetLastError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
    }

    // Set the locale and return success
    hf->pHash->lcLocale = (USHORT)lcNewLocale;
    hf->ha->dwFlags |= MPQ_FLAG_CHANGED;
    return TRUE;
}

//-----------------------------------------------------------------------------
// Sets add file callback

BOOL WINAPI SFileSetAddFileCallback(HANDLE /* hMpq */, SFILE_ADDFILE_CALLBACK aAddFileCB, void * pvData)
{
    pvUserData = pvData;
    AddFileCB = aAddFileCB;
    return TRUE;
}
