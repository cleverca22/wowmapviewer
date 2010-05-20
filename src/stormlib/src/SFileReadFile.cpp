/*****************************************************************************/
/* SFileReadFile.cpp                      Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* Description :                                                             */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* xx.xx.99  1.00  Lad  The first version of SFileReadFile.cpp               */
/* 24.03.99  1.00  Lad  Added the SFileGetFileInfo function                  */
/*****************************************************************************/

#define __STORMLIB_SELF__
#define __INCLUDE_COMPRESSION__
#include "StormLib.h"
#include "SCommon.h"

//-----------------------------------------------------------------------------
// Defines

#define ID_WAVE     0x46464952          // Signature of WAVes for name breaking
#define ID_EXE      0x00005A4D          // Signature of executable files

//-----------------------------------------------------------------------------
// Local structures

struct TID2Ext
{
    DWORD dwID;
    char * szExt;
};

//-----------------------------------------------------------------------------
// Local functions

//  hf            - MPQ File handle.
//  pbBuffer      - Pointer to target buffer to store sectors.
//  dwByteOffset  - Position of sector in the file (relative to file begin)
//  dwBytesToRead - Number of bytes to read. Must be multiplier of sector size.
//  pdwBytesRead  - Stored number of bytes loaded
static int ReadMpqSectors(TMPQFile * hf, BYTE * pbBuffer, DWORD dwByteOffset, DWORD dwBytesToRead, DWORD * pdwBytesRead)
{
    LARGE_INTEGER RawFilePos;
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    BYTE * pbRawSector = NULL;
    BYTE * pbOutSector = pbBuffer;
    BYTE * pbInSector = pbBuffer;
    DWORD dwRawBytesToRead;
    DWORD dwRawSectorOffset = dwByteOffset;
    DWORD dwSectorsToRead = dwBytesToRead / ha->dwSectorSize;
    DWORD dwSectorIndex = dwByteOffset / ha->dwSectorSize;
    DWORD dwSectorsDone = 0;
    DWORD dwBytesRead = 0;
    int nError = ERROR_SUCCESS;

    // Note that dwByteOffset must be aligned to size of one sector
    // Note that dwBytesToRead must be a multiplier of one sector size
    // This is local function, so we won't check if that's true.
    // Note that files stored in single units are processed by a separate function

    // If there is not enough bytes remaining, cut dwBytesToRead
    if((dwByteOffset + dwBytesToRead) > pBlock->dwFSize)
        dwBytesToRead = pBlock->dwFSize - dwByteOffset;
    dwRawBytesToRead = dwBytesToRead;

    // Perform all necessary work to do with compressed files
    if(pBlock->dwFlags & MPQ_FILE_COMPRESSED)
    {
        // If the sector positions are not loaded yet, do it
        if(hf->SectorOffsets == NULL)
        {
            nError = AllocateSectorOffsets(hf, true);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // If the sector checksums are not loaded yet, load them now.
        if(hf->SectorChksums == NULL && (pBlock->dwFlags & MPQ_FILE_SECTOR_CRC))
        {
            nError = AllocateSectorChecksums(hf, true);
            if(nError != ERROR_SUCCESS)
                return nError;
        }

        // If the file is compressed, also allocate secondary buffer
        pbInSector = pbRawSector = ALLOCMEM(BYTE, dwBytesToRead);
        if(pbRawSector == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Assign the temporary buffer as target for ReadFile operation
        dwRawSectorOffset = hf->SectorOffsets[dwSectorIndex];
        dwRawBytesToRead = hf->SectorOffsets[dwSectorIndex + dwSectorsToRead] - dwRawSectorOffset;
    }

    // Calculate raw file offset where the sector(s) are stored.
    CalculateRawSectorOffset(RawFilePos, hf, dwRawSectorOffset);

    // Set file pointer and read all required sectors
    SetFilePointer(ha->hFile, RawFilePos.LowPart, &RawFilePos.HighPart, FILE_BEGIN);
    ReadFile(ha->hFile, pbInSector, dwRawBytesToRead, &dwBytesRead, NULL);
    if(dwBytesRead != dwRawBytesToRead)
        return ERROR_FILE_CORRUPT;
    dwBytesRead = 0;

    // Now we have to decrypt and decompress all file sectors that have been loaded
    for(DWORD i = 0; i < dwSectorsToRead; i++)
    {
        DWORD dwRawBytesInThisSector = ha->dwSectorSize;
        DWORD dwBytesInThisSector = ha->dwSectorSize;
        DWORD dwIndex = dwSectorIndex + i;

        // If there is not enough bytes in the last sector,
        // cut the number of bytes in this sector
        if(dwRawBytesInThisSector > dwBytesToRead)
            dwRawBytesInThisSector = dwBytesToRead;
        if(dwBytesInThisSector > dwBytesToRead)
            dwBytesInThisSector = dwBytesToRead;

        // If the file is compressed, we have to adjust the raw sector size
        if(hf->pBlock->dwFlags & MPQ_FILE_COMPRESSED)
            dwRawBytesInThisSector = hf->SectorOffsets[dwIndex + 1] - hf->SectorOffsets[dwIndex];

        // If the file is encrypted, we have to decrypt the sector
        if(hf->pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
        {
            BSWAP_ARRAY32_UNSIGNED(pbInSector, dwRawBytesInThisSector);

            // If we don't know the key, try to detect it by file content
            if(hf->dwFileKey == 0)
            {
                hf->dwFileKey = DetectFileKeyByContent(pbInSector, dwBytesInThisSector);
                if(hf->dwFileKey == 0)
                {
                    nError = ERROR_UNKNOWN_FILE_KEY;
                    break;
                }
            }

            DecryptMpqBlock(pbInSector, dwRawBytesInThisSector, hf->dwFileKey + dwIndex);
            BSWAP_ARRAY32_UNSIGNED(pbInSector, dwRawBytesInThisSector);
        }

        // If the file has sector CRC check turned on, perform it
        if(hf->bCheckSectorCRCs && hf->SectorChksums != NULL)
        {
            DWORD dwAdlerExpected = hf->SectorChksums[dwIndex];
            DWORD dwAdlerValue = 0;

            // We can only check sector CRC when it's not zero
            // Neither can we check it if it's 0xFFFFFFFF.
            if(dwAdlerExpected != 0 && dwAdlerExpected != 0xFFFFFFFF)
            {
                dwAdlerValue = adler32(0, pbInSector, dwRawBytesInThisSector);
                if(dwAdlerValue != dwAdlerExpected)
                {
                    nError = ERROR_CHECKSUM_ERROR;
                    break;
                }
            }
        }

        // If the sector is really compressed, decompress it.
        // WARNING : Some sectors may not be compressed, it can be determined only
        // by comparing uncompressed and compressed size !!!
        if(dwRawBytesInThisSector < dwBytesInThisSector)
        {
            int cbOutSector = dwBytesInThisSector;
            int cbInSector = dwRawBytesInThisSector;
            int nResult = 0;

            // Is the file compressed by PKWARE Data Compression Library ?
            if(hf->pBlock->dwFlags & MPQ_FILE_IMPLODE)
                nResult = SCompExplode((char *)pbOutSector, &cbOutSector, (char *)pbInSector, cbInSector);

            // Is the file compressed by Blizzard's multiple compression ?
            if(hf->pBlock->dwFlags & MPQ_FILE_COMPRESS)
                nResult = SCompDecompress((char *)pbOutSector, &cbOutSector, (char *)pbInSector, cbInSector);

            // Did the decompression fail ?
            if(nResult == 0)
            {
                nError = ERROR_FILE_CORRUPT;
                break;
            }
        }
        else
        {
            if(pbOutSector != pbInSector)
                memcpy(pbOutSector, pbInSector, dwBytesInThisSector);
        }

        // Move pointers
        dwBytesToRead -= dwBytesInThisSector;
        dwByteOffset += dwBytesInThisSector;
        dwBytesRead += dwBytesInThisSector;
        pbOutSector += dwBytesInThisSector;
        pbInSector += dwRawBytesInThisSector;
        dwSectorsDone++;
    }

    // Free all used buffers
    if(pbRawSector != NULL)
        FREEMEM(pbRawSector);
    
    // Give the caller thenumber of bytes read
    *pdwBytesRead = dwBytesRead;
    return nError; 
}

static int ReadMpqFileSingleUnit(TMPQFile * hf, VOID * pvBuffer, DWORD dwToRead, DWORD * pdwBytesRead)
{
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    BYTE * pbCompressed = NULL;
    BYTE * pbRawData = NULL;
    DWORD dwBytesRead = 0;

    // If the file buffer is not allocated yet, do it, and reload the buffer
    if(hf->pbFileSector == NULL)
    {
        // Allocate buffer for the entire file
        hf->pbFileSector = ALLOCMEM(BYTE, pBlock->dwFSize);
        if(hf->pbFileSector == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;
        hf->dwSectorOffs = SFILE_INVALID_POS;
        pbRawData = hf->pbFileSector;
    }

    // If the file buffer is not loaded yet, do it
    if(hf->dwSectorOffs != 0)
    {
        // If the file is compressed, we have to allocate buffer for compressed data
        if(hf->pBlock->dwCSize < hf->pBlock->dwFSize)
        {
            pbCompressed = ALLOCMEM(BYTE, hf->pBlock->dwCSize);
            if(pbCompressed == NULL)
                return ERROR_NOT_ENOUGH_MEMORY;
            pbRawData = pbCompressed;
        }

        // Read the entire file
        SetFilePointer(ha->hFile, hf->RawFilePos.LowPart, &hf->RawFilePos.HighPart, FILE_BEGIN);
        ReadFile(ha->hFile, pbRawData, pBlock->dwCSize, &dwBytesRead, NULL);
        if(dwBytesRead != pBlock->dwCSize)
        {
            FREEMEM(pbCompressed);
            return ERROR_FILE_CORRUPT;
        }

        // If the file is encrypted, we have to decrypt the data first
        if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
        {
            BSWAP_ARRAY32_UNSIGNED(pbRawData, pBlock->dwCSize);
            DecryptMpqBlock(pbRawData, pBlock->dwCSize, hf->dwFileKey);
            BSWAP_ARRAY32_UNSIGNED(pbRawData, pBlock->dwCSize);
        }

        // If the file is compressed, we have to decompress it now
        if(pBlock->dwCSize < pBlock->dwFSize)
        {
            int cbOutBuffer = (int)pBlock->dwFSize;
            int nResult = 0;

            // Note: Single unit files compressed with IMPLODE are not supported by Blizzard
            if(pBlock->dwFlags & MPQ_FILE_IMPLODE)
                nResult = SCompExplode((char *)hf->pbFileSector, &cbOutBuffer, (char *)pbRawData, (int)pBlock->dwCSize);
            if(pBlock->dwFlags & MPQ_FILE_COMPRESS)
                nResult = SCompDecompress((char *)hf->pbFileSector, &cbOutBuffer, (char *)pbRawData, (int)pBlock->dwCSize);

            // Free the decompression buffer.
            FREEMEM(pbCompressed);
            if(nResult == 0)
                return ERROR_FILE_CORRUPT;
        }

        // The sector is now properly loaded
        hf->dwSectorOffs = 0;
    }

    // At this moment, we have the file loaded into the file buffer.
    // Copy as much as the caller wants
    if(hf->dwSectorOffs == 0)
    {
        // File position is greater or equal to file size ?
        if(hf->dwFilePos >= pBlock->dwFSize)
        {
            *pdwBytesRead = 0;
            return ERROR_SUCCESS;
        }

        // If not enough bytes remaining in the file, cut them
        if((pBlock->dwFSize - hf->dwFilePos) < dwToRead)
            dwToRead = (pBlock->dwFSize - hf->dwFilePos);

        // Copy the bytes
        memcpy(pvBuffer, hf->pbFileSector + hf->dwFilePos, dwToRead);
        hf->dwFilePos += dwToRead;

        // Give the number of bytes read
        *pdwBytesRead = dwToRead;
        return ERROR_SUCCESS;
    }

    // An error, sorry
    return ERROR_READ_FAULT;
}

static int ReadMpqFile(TMPQFile * hf, VOID * pvBuffer, DWORD dwBytesToRead, DWORD * pdwBytesRead)
{
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    BYTE * pbBuffer = (BYTE *)pvBuffer;
    DWORD dwTotalBytesRead = 0;                         // Total bytes read in all three parts
    DWORD dwSectorSizeMask = ha->dwSectorSize - 1;      // Mask for block size, usually 0x0FFF
    DWORD dwFileSectorPos;                              // File offset of the loaded sector
    DWORD dwBytesRead;                                  // Number of bytes read (temporary variable)
    int nError;

    // If the file position is at or beyond end of file, do nothing
    if(hf->dwFilePos >= pBlock->dwFSize)
    {
        *pdwBytesRead = 0;
        return ERROR_SUCCESS;
    }

    // If not enough bytes in the file remaining, cut them
    if(dwBytesToRead > (pBlock->dwFSize - hf->dwFilePos))
        dwBytesToRead = (pBlock->dwFSize - hf->dwFilePos);

    // Compute sector position in the file
    dwFileSectorPos = hf->dwFilePos & ~dwSectorSizeMask;  // Position in the block

    // If the file sector buffer is not allocated yet, do it now
    if(hf->pbFileSector == NULL)
    {
        nError = AllocateSectorBuffer(hf);
        if(nError != ERROR_SUCCESS)
            return nError;
    }

    // Load the first (incomplete) file sector
    if(hf->dwFilePos & dwSectorSizeMask)
    {
        DWORD dwBytesInSector = ha->dwSectorSize;
        DWORD dwBufferOffs = hf->dwFilePos & dwSectorSizeMask;
        DWORD dwToCopy;                                     

        // Is the file sector already loaded ?
        if(hf->dwSectorOffs != dwFileSectorPos)
        {
            // Load one MPQ sector into archive buffer
            nError = ReadMpqSectors(hf, hf->pbFileSector, dwFileSectorPos, ha->dwSectorSize, &dwBytesInSector);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Remember that the data loaded to the sector have new file offset
            hf->dwSectorOffs = dwFileSectorPos;
        }
        else
        {
            if((dwFileSectorPos + dwBytesInSector) > pBlock->dwFSize)
                dwBytesInSector = pBlock->dwFSize - dwFileSectorPos;
        }

        // Copy the data from the offset in the loaded sector to the end of the sector
        dwToCopy = dwBytesInSector - dwBufferOffs;
        if(dwToCopy > dwBytesToRead)
            dwToCopy = dwBytesToRead;

        // Copy data from sector buffer into target buffer
        memcpy(pbBuffer, hf->pbFileSector + dwBufferOffs, dwToCopy);

        // Update pointers and byte counts
        dwTotalBytesRead += dwToCopy;
        dwFileSectorPos  += dwBytesInSector;
        pbBuffer         += dwToCopy;
        dwBytesToRead    -= dwToCopy;
    }

    // Load the whole ("middle") sectors only if there is at least one full sector to be read
    if(dwBytesToRead >= ha->dwSectorSize)
    {
        DWORD dwBlockBytes = dwBytesToRead & ~dwSectorSizeMask;

        // Load all sectors to the output buffer
        nError = ReadMpqSectors(hf, pbBuffer, dwFileSectorPos, dwBlockBytes, &dwBytesRead);
        if(nError != ERROR_SUCCESS)
            return nError;

        // Update pointers
        dwTotalBytesRead += dwBytesRead;
        dwFileSectorPos  += dwBytesRead;
        pbBuffer         += dwBytesRead;
        dwBytesToRead    -= dwBytesRead;
    }

    // Read the terminating sector
    if(dwBytesToRead > 0)
    {
        DWORD dwToCopy = ha->dwSectorSize;

        // Is the file sector already loaded ?
        if(hf->dwSectorOffs != dwFileSectorPos)
        {
            // Load one MPQ sector into archive buffer
            nError = ReadMpqSectors(hf, hf->pbFileSector, dwFileSectorPos, ha->dwSectorSize, &dwBytesRead);
            if(nError != ERROR_SUCCESS)
                return nError;

            // Remember that the data loaded to the sector have new file offset
            hf->dwSectorOffs = dwFileSectorPos;
        }

        // Check number of bytes read
        if(dwToCopy > dwBytesToRead)
            dwToCopy = dwBytesToRead;

        // Copy the data from the cached last sector to the caller's buffer
        memcpy(pbBuffer, hf->pbFileSector, dwToCopy);
        
        // Update pointers
        dwTotalBytesRead += dwToCopy;
    }

    // Update the file position
    hf->dwFilePos += dwTotalBytesRead;

    // Store total number of bytes read to the caller
    *pdwBytesRead = dwTotalBytesRead;
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// SFileReadFile

BOOL WINAPI SFileReadFile(HANDLE hFile, VOID * pvBuffer, DWORD dwToRead, DWORD * pdwRead, LPOVERLAPPED lpOverlapped)
{
    TMPQFile * hf = (TMPQFile *)hFile;
    DWORD dwBytesRead = 0;                      // Number of bytes read
    int nError = ERROR_SUCCESS;

    // Check valid parameters
    if(IsValidFileHandle(hf) == FALSE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if(pvBuffer == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // If the file is local file, redirect the read request to ReadFile
    if(hf->hFile != INVALID_HANDLE_VALUE)
    {
        if(!ReadFile(hf->hFile, pvBuffer, dwToRead, &dwBytesRead, lpOverlapped))
            nError = ERROR_READ_FAULT;
    }

    // If the file is single unit file, redirect it to read file 
    else if(hf->pBlock->dwFlags & MPQ_FILE_SINGLE_UNIT)
    {
        nError = ReadMpqFileSingleUnit(hf, pvBuffer, dwToRead, &dwBytesRead);
    }

    // Otherwise read it as sector based MPQ file
    else
    {
        nError = ReadMpqFile(hf, pvBuffer, dwToRead, &dwBytesRead);
    }

    // Give the caller the number of bytes read
    if(pdwRead != NULL)
        *pdwRead = dwBytesRead;

    // If the read operation succeeded, but not full number of bytes was read,
    // set the last error to ERROR_HANDLE_EOF
    if(nError == ERROR_SUCCESS && (dwBytesRead < dwToRead))
        nError = ERROR_HANDLE_EOF;

    // If something failed, set the last error value
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// SFileGetFileSize

DWORD WINAPI SFileGetFileSize(HANDLE hFile, DWORD * pdwFileSizeHigh)
{
    TMPQFile * hf = (TMPQFile *)hFile;

    if(IsValidFileHandle(hf))
    {
        // Is it a local file ?
        if(hf->hFile != INVALID_HANDLE_VALUE)
            return GetFileSize(hf->hFile, pdwFileSizeHigh);

        // If opened from archive, return file size
        if(pdwFileSizeHigh != NULL)
            *pdwFileSizeHigh = 0;
        return hf->pBlock->dwFSize;
    }

    SetLastError(ERROR_INVALID_HANDLE);
    return SFILE_INVALID_SIZE;
}

DWORD WINAPI SFileSetFilePointer(HANDLE hFile, LONG lFilePos, LONG * plFilePosHigh, DWORD dwMoveMethod)
{
    TMPQArchive * ha;
    TMPQFile * hf = (TMPQFile *)hFile;

    // If the hFile is not a valid file handle, return an error.
    if(IsValidFileHandle(hf) == FALSE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return SFILE_INVALID_SIZE;
    }

    // If opened as plain file, call Win32 API
    if(hf->hFile != INVALID_HANDLE_VALUE)
        return SetFilePointer(hf->hFile, lFilePos, plFilePosHigh, dwMoveMethod);
    ha = hf->ha;

    // If the plFilePosHigh is not NULL, it must point to NULL.
    if(plFilePosHigh != NULL && *plFilePosHigh != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return SFILE_INVALID_SIZE;
    }

    switch(dwMoveMethod)
    {
        case FILE_BEGIN:
            // Cannot set pointer before begin of file
            if(-lFilePos > (LONG)hf->dwFilePos)
                hf->dwFilePos = 0;
            else
                hf->dwFilePos = lFilePos;
            break;

        case FILE_CURRENT:
            // Cannot set pointer before begin of file
            if(-lFilePos > (LONG)hf->dwFilePos)
                hf->dwFilePos = 0;
            else
                hf->dwFilePos += lFilePos;
            break;

        case FILE_END:
            // Cannot set file position before begin of file
            if(-lFilePos >= (LONG)hf->pBlock->dwFSize)
                hf->dwFilePos = 0;
            else
                hf->dwFilePos = hf->pBlock->dwFSize + lFilePos;
            break;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return SFILE_INVALID_SIZE;
    }

    return hf->dwFilePos;
}

//-----------------------------------------------------------------------------
// Tries to retrieve the file name

static TID2Ext id2ext[] = 
{
    {0x1A51504D, "mpq"},                // MPQ archive header ID ('MPQ\x1A')
    {0x46464952, "wav"},                // WAVE header 'RIFF'
    {0x324B4D53, "smk"},                // Old "Smacker Video" files 'SMK2'
    {0x694B4942, "bik"},                // Bink video files (new)
    {0x0801050A, "pcx"},                // PCX images used in Diablo I
    {0x544E4F46, "fnt"},                // Font files used in Diablo II
    {0x6D74683C, "html"},               // HTML '<htm'
    {0x4D54483C, "html"},               // HTML '<HTM
    {0x216F6F57, "tbl"},                // Table files
    {0x31504C42, "blp"},                // BLP textures
    {0x32504C42, "blp"},                // BLP textures (v2)
    {0x584C444D, "mdx"},                // MDX files
    {0x45505954, "pud"},                // Warcraft II maps
    {0x38464947, "gif"},                // GIF images 'GIF8'
    {0x3032444D, "m2"},                 // WoW ??? .m2
    {0x43424457, "dbc"},                // ??? .dbc
    {0x47585053, "bls"},                // WoW pixel shaders
    {0xE0FFD8FF, "jpg"},                // JPEG image
    {0, NULL}                           // Terminator 
};

BOOL WINAPI SFileGetFileName(HANDLE hFile, char * szFileName)
{
    TMPQFile * hf = (TMPQFile *)hFile;  // MPQ File handle
    char * szExt = "xxx";               // Default extension
    DWORD dwFirstBytes[2];              // The first 4 bytes of the file
    DWORD dwFilePos;                    // Saved file position
    int nError = ERROR_SUCCESS;
    int i;

    // Pre-zero the output buffer
    if(szFileName != NULL)
        *szFileName = 0;

    // Check valid parameters
    if(IsValidFileHandle(hf) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(szFileName == NULL)
        nError = ERROR_INVALID_PARAMETER;
    
    // If the file name is already filled, return it.
    if(nError == ERROR_SUCCESS && *hf->szFileName != 0)
    {
        if(szFileName != hf->szFileName)
            strcpy(szFileName, hf->szFileName);
        return TRUE;
    }

    if(nError == ERROR_SUCCESS)
    {
        if(hf->dwBlockIndex == (DWORD)-1)
            nError = ERROR_CAN_NOT_COMPLETE;
    }

    // Read the first 8 bytes from the file
    if(nError == ERROR_SUCCESS)
    {
        dwFirstBytes[0] = dwFirstBytes[1] = 0;
        dwFilePos = SFileSetFilePointer(hf, 0, NULL, FILE_CURRENT);   
        SFileReadFile(hFile, &dwFirstBytes, sizeof(dwFirstBytes), NULL);
        BSWAP_ARRAY32_UNSIGNED(dwFirstBytes, sizeof(dwFirstBytes));
        SFileSetFilePointer(hf, dwFilePos, NULL, FILE_BEGIN);
    }

    if(nError == ERROR_SUCCESS)
    {
        if((dwFirstBytes[0] & 0x0000FFFF) == ID_EXE)
            szExt = "exe";
        else if(dwFirstBytes[0] == 0x00000006 && dwFirstBytes[1] == 0x00000001)
            szExt = "dc6";
        else
        {
            for(i = 0; id2ext[i].szExt != NULL; i++)
            {
                if(id2ext[i].dwID == dwFirstBytes[0])
                {
                    szExt = id2ext[i].szExt;
                    break;
                }
            }
        }

        // Create the file name
        sprintf(hf->szFileName, "File%08lu.%s", hf->dwBlockIndex, szExt);
        if(szFileName != hf->szFileName)
            strcpy(szFileName, hf->szFileName);
    }
    return (nError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// Retrieves an information about an archive or about a file within the archive
//
//  hMpqOrFile - Handle to an MPQ archive or to a file
//  dwInfoType - Information to obtain

#ifndef __USE_OLD_GETFILEINFO__

#define VERIFY_MPQ_HANDLE(h)                \
    if(!IsValidMpqHandle(h))                \
    {                                       \
        nError = ERROR_INVALID_HANDLE;      \
        break;                              \
    }

#define VERIFY_FILE_HANDLE(h)               \
    if(!IsValidFileHandle(h))               \
    {                                       \
        nError = ERROR_INVALID_HANDLE;      \
        break;                              \
    }

#define GIVE_32BIT_VALUE(val)               \
    cbLengthNeeded = sizeof(DWORD);         \
    if(cbFileInfo < cbLengthNeeded)         \
    {                                       \
        nError = ERROR_INSUFFICIENT_BUFFER; \
        break;                              \
    }                                       \
    *((DWORD *)pvFileInfo) = val;


BOOL WINAPI SFileGetFileInfo(
    HANDLE hMpqOrFile,
    DWORD dwInfoType,
    VOID * pvFileInfo,
    DWORD cbFileInfo,
    DWORD * pcbLengthNeeded)
{
    TMPQFileTime * ft;
    TMPQArchive * ha = (TMPQArchive *)hMpqOrFile;
    TMPQBlock * pBlockEnd;
    TMPQBlock * pBlock;
    TMPQFile * hf = (TMPQFile *)hMpqOrFile;
    DWORD cbLengthNeeded = 0;
    DWORD dwFileCount = 0;
    DWORD dwFileKey;
    int nError = ERROR_SUCCESS;

    switch(dwInfoType)
    {
        case SFILE_INFO_ARCHIVE_SIZE:       // Size of the archive
            VERIFY_MPQ_HANDLE(ha);
            GIVE_32BIT_VALUE(ha->pHeader->dwArchiveSize);
            break;

        case SFILE_INFO_HASH_TABLE_SIZE:    // Size of the hash table
            VERIFY_MPQ_HANDLE(ha);
            GIVE_32BIT_VALUE(ha->pHeader->dwHashTableSize);
            break;

        case SFILE_INFO_BLOCK_TABLE_SIZE:   // Size of the block table
            VERIFY_MPQ_HANDLE(ha);
            GIVE_32BIT_VALUE(ha->pHeader->dwBlockTableSize);
            break;

        case SFILE_INFO_SECTOR_SIZE:
            VERIFY_MPQ_HANDLE(ha);
            GIVE_32BIT_VALUE(ha->dwSectorSize);
            break;

        case SFILE_INFO_HASH_TABLE:
            VERIFY_MPQ_HANDLE(ha);
            cbLengthNeeded = ha->pHeader->dwHashTableSize * sizeof(TMPQHash);
            if(cbFileInfo < cbLengthNeeded)
            {
                nError = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            memcpy(pvFileInfo, ha->pHashTable, cbLengthNeeded);
            break;

        case SFILE_INFO_BLOCK_TABLE:
            VERIFY_MPQ_HANDLE(ha);
            cbLengthNeeded = ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock);
            if(cbFileInfo < cbLengthNeeded)
            {
                nError = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
            memcpy(pvFileInfo, ha->pBlockTable, cbLengthNeeded);
            break;

        case SFILE_INFO_NUM_FILES:
            VERIFY_MPQ_HANDLE(ha);
            pBlockEnd = ha->pBlockTable + ha->pHeader->dwBlockTableSize;
            for(pBlock = ha->pBlockTable; pBlock < pBlockEnd; pBlock++)
            {
                if(pBlock->dwFlags & MPQ_FILE_EXISTS)
                    dwFileCount++;
            }
            GIVE_32BIT_VALUE(dwFileCount);
            break;

        case SFILE_INFO_HASH_INDEX:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->dwHashIndex);
            break;

        case SFILE_INFO_CODENAME1:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pHash->dwName1);
            break;

        case SFILE_INFO_CODENAME2:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pHash->dwName2);
            break;

        case SFILE_INFO_LOCALEID:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pHash->lcLocale);
            break;

        case SFILE_INFO_BLOCKINDEX:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->dwBlockIndex);
            break;

        case SFILE_INFO_FILE_SIZE:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pBlock->dwFSize);
            break;

        case SFILE_INFO_COMPRESSED_SIZE:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pBlock->dwCSize);
            break;

        case SFILE_INFO_FLAGS:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pBlock->dwFlags);
            break;

        case SFILE_INFO_POSITION:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->pBlock->dwFilePos);
            break;

        case SFILE_INFO_KEY:
            VERIFY_FILE_HANDLE(hf);
            GIVE_32BIT_VALUE(hf->dwFileKey);
            break;

        case SFILE_INFO_KEY_UNFIXED:
            VERIFY_FILE_HANDLE(hf);
            dwFileKey = hf->dwFileKey;
            if(hf->pBlock->dwFlags & MPQ_FILE_FIX_KEY)
                dwFileKey = (dwFileKey ^ hf->pBlock->dwFSize) - hf->MpqFilePos.LowPart;
            GIVE_32BIT_VALUE(dwFileKey);
            break;

        case SFILE_INFO_FILETIME:
            VERIFY_FILE_HANDLE(hf);
            cbLengthNeeded = sizeof(TMPQFileTime);
            if(cbFileInfo < cbLengthNeeded)
            {
                nError = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            // Pre-fill the filetime with zeros
            ft = (TMPQFileTime *)pvFileInfo;
            ft->dwFileTimeHigh = ft->dwFileTimeLow = 0;
            if(hf->pFileTime != NULL)
                *ft = *hf->pFileTime;
            break;

        default:
            nError = ERROR_INVALID_PARAMETER;
            break;
    }

    // If the caller specified pointer to length needed,
    // give it to him
    if(pcbLengthNeeded != NULL)
        *pcbLengthNeeded = cbLengthNeeded;
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

#else
// Old version
DWORD_PTR WINAPI SFileGetFileInfo(HANDLE hMpqOrFile, DWORD dwInfoType)
{
    TMPQArchive * ha = (TMPQArchive *)hMpqOrFile;
    TMPQFile    * hf = (TMPQFile *)hMpqOrFile;
    TMPQBlock   * pBlockEnd;
    TMPQBlock   * pBlock;
    DWORD dwFileCount = 0;
    DWORD dwFileKey;

    switch(dwInfoType)
    {
        case SFILE_INFO_ARCHIVE_SIZE:
            if(IsValidMpqHandle(ha))
                return ha->pHeader->dwArchiveSize;
            break;

        case SFILE_INFO_HASH_TABLE_SIZE:    // Size of the hash table
            if(IsValidMpqHandle(ha))
                return ha->pHeader->dwHashTableSize;
            break;

        case SFILE_INFO_BLOCK_TABLE_SIZE:   // Size of the block table
            if(IsValidMpqHandle(ha))
                return ha->pHeader->dwBlockTableSize;
            break;

        case SFILE_INFO_SECTOR_SIZE:
            if(IsValidMpqHandle(ha))
                return ha->dwSectorSize;
            break;

        case SFILE_INFO_HASH_TABLE:
            if(IsValidMpqHandle(ha))
                return (DWORD_PTR)ha->pHashTable;
            break;

        case SFILE_INFO_BLOCK_TABLE:
            if(IsValidMpqHandle(ha))
                return (DWORD_PTR)ha->pBlockTable;
            break;

        case SFILE_INFO_NUM_FILES:
            if(IsValidMpqHandle(ha))
            {
                pBlockEnd = ha->pBlockTable + ha->pHeader->dwBlockTableSize;
                for(pBlock = ha->pBlockTable; pBlock < pBlockEnd; pBlock++)
                {
                    if(pBlock->dwFlags & MPQ_FILE_EXISTS)
                        dwFileCount++;
                }
                return dwFileCount;
            }
            break;

        case SFILE_INFO_HASH_INDEX:
            if(IsValidFileHandle(hf))
                return hf->dwHashIndex;
            break;

        case SFILE_INFO_CODENAME1:
            if(IsValidFileHandle(hf))
                return hf->pHash->dwName1;
            break;

        case SFILE_INFO_CODENAME2:
            if(IsValidFileHandle(hf))
                return hf->pHash->dwName2;
            break;

        case SFILE_INFO_LOCALEID:
            if(IsValidFileHandle(hf))
                return hf->pHash->lcLocale;
            break;

        case SFILE_INFO_BLOCKINDEX:
            if(IsValidFileHandle(hf))
                return hf->dwBlockIndex;
            break;

        case SFILE_INFO_FILE_SIZE:
            if(IsValidFileHandle(hf))
                return hf->pBlock->dwFSize;
            break;

        case SFILE_INFO_COMPRESSED_SIZE:
            if(IsValidFileHandle(hf))
                return hf->pBlock->dwCSize;
            break;

        case SFILE_INFO_FLAGS:
            if(IsValidFileHandle(hf))
                return hf->pBlock->dwFlags;
            break;

        case SFILE_INFO_POSITION:
            if(IsValidFileHandle(hf))
                return hf->pBlock->dwFilePos;
            break;

        case SFILE_INFO_KEY:
            if(IsValidFileHandle(hf))
                return hf->dwFileKey;
            break;

        case SFILE_INFO_KEY_UNFIXED:
            if(IsValidFileHandle(hf))
            {
                dwFileKey = hf->dwFileKey;
                if(hf->pBlock->dwFlags & MPQ_FILE_FIX_KEY)
                    dwFileKey = (dwFileKey ^ hf->pBlock->dwFSize) - hf->MpqFilePos.LowPart;
                return dwFileKey;
            }
            break;
    }

    // Unknown parameter or invalid handle
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0xFFFFFFFF;
}
#endif
