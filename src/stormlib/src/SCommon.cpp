/*****************************************************************************/
/* SCommon.cpp                            Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* Common functions for StormLib, used by all SFile*** modules               */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 24.03.03  1.00  Lad  The first version of SFileCommon.cpp                 */
/* 19.11.03  1.01  Dan  Big endian handling                                  */
/* 12.06.04  1.01  Lad  Renamed to SCommon.cpp                               */
/*****************************************************************************/

#define __STORMLIB_SELF__
#define __INCLUDE_CRYPTOGRAPHY__
#include "StormLib.h"
#include "SCommon.h"

char StormLibCopyright[] = "StormLib v " STORMLIB_VERSION_STRING " Copyright Ladislav Zezula 1998-2010";

//-----------------------------------------------------------------------------
// The buffer for decryption engine.

LCID    lcFileLocale  = LANG_NEUTRAL;       // File locale
USHORT  wPlatform = 0;                      // File platform

//-----------------------------------------------------------------------------
// Storm buffer functions

#define MPQ_HASH_TABLE_OFFSET   0x000
#define MPQ_HASH_NAME_A         0x100
#define MPQ_HASH_NAME_B         0x200
#define MPQ_HASH_FILE_KEY       0x300

#define STORM_BUFFER_SIZE   0x500

static DWORD StormBuffer[STORM_BUFFER_SIZE];    // Buffer for the decryption engine
static BOOL  bMpqCryptographyInitialized = FALSE;

static DWORD HashString(const char * szFileName, DWORD dwHashType)
{
    BYTE * pbKey   = (BYTE *)szFileName;
    DWORD  dwSeed1 = 0x7FED7FED;
    DWORD  dwSeed2 = 0xEEEEEEEE;
    DWORD  ch;

    while(*pbKey != 0)
    {
        ch = toupper(*pbKey++);

        dwSeed1 = StormBuffer[dwHashType + ch] ^ (dwSeed1 + dwSeed2);
        dwSeed2 = ch + dwSeed1 + dwSeed2 + (dwSeed2 << 5) + 3;
    }

    return dwSeed1;
}

void InitializeMpqCryptography()
{
    DWORD dwSeed = 0x00100001;
    DWORD index1 = 0;
    DWORD index2 = 0;
    int   i;

    // Initialize the decryption buffer.
    // Do nothing if already done.
    if(bMpqCryptographyInitialized == FALSE)
    {
        for(index1 = 0; index1 < 0x100; index1++)
        {
            for(index2 = index1, i = 0; i < 5; i++, index2 += 0x100)
            {
                DWORD temp1, temp2;

                dwSeed = (dwSeed * 125 + 3) % 0x2AAAAB;
                temp1  = (dwSeed & 0xFFFF) << 0x10;

                dwSeed = (dwSeed * 125 + 3) % 0x2AAAAB;
                temp2  = (dwSeed & 0xFFFF);

                StormBuffer[index2] = (temp1 | temp2);
            }
        }

        // Also register both MD5 and SHA1 hash algorithms
        register_hash(&md5_desc);
        register_hash(&sha1_desc);

        // Use LibTomMath as support math library for LibTomCrypt
        ltc_mp = ltm_desc;    

        // Don't do that again
        bMpqCryptographyInitialized = TRUE;
    }
}

//-----------------------------------------------------------------------------
// Functions decrypts the file key from the file name

DWORD DecryptFileKey(const char * szFileName)
{
    return HashString(szFileName, MPQ_HASH_FILE_KEY);
}

//-----------------------------------------------------------------------------
// Encrypting and decrypting MPQ file data

void EncryptMpqBlock(VOID * pvFileBlock, DWORD dwLength, DWORD dwSeed1)
{
    DWORD * block = (DWORD *)pvFileBlock;
    DWORD dwSeed2 = 0xEEEEEEEE;
    DWORD ch;

    // Round to DWORDs
    dwLength >>= 2;

    while(dwLength-- > 0)
    {
        dwSeed2 += StormBuffer[0x400 + (dwSeed1 & 0xFF)];
        ch     = *block;
        *block++ = ch ^ (dwSeed1 + dwSeed2);

        dwSeed1  = ((~dwSeed1 << 0x15) + 0x11111111) | (dwSeed1 >> 0x0B);
        dwSeed2  = ch + dwSeed2 + (dwSeed2 << 5) + 3;
    }
}

void DecryptMpqBlock(VOID * pvFileBlock, DWORD dwLength, DWORD dwSeed1)
{
    DWORD * block = (DWORD *)pvFileBlock;
    DWORD dwSeed2 = 0xEEEEEEEE;
    DWORD ch;

    // Round to DWORDs
    dwLength >>= 2;

    while(dwLength-- > 0)
    {
        dwSeed2 += StormBuffer[0x400 + (dwSeed1 & 0xFF)];
        ch     = *block ^ (dwSeed1 + dwSeed2);

        dwSeed1  = ((~dwSeed1 << 0x15) + 0x11111111) | (dwSeed1 >> 0x0B);
        dwSeed2  = ch + dwSeed2 + (dwSeed2 << 5) + 3;
        *block++ = ch;
    }
}

void EncryptMpqTable(VOID * pvMpqTable, DWORD dwLength, const char * szKey)
{
    EncryptMpqBlock(pvMpqTable, dwLength, HashString(szKey, MPQ_HASH_FILE_KEY));
}

void DecryptMpqTable(VOID * pvMpqTable, DWORD dwLength, const char * szKey)
{
    DecryptMpqBlock(pvMpqTable, dwLength, HashString(szKey, MPQ_HASH_FILE_KEY));
}

//-----------------------------------------------------------------------------
// Functions tries to get file decryption key. The trick comes from sector
// positions which are stored at the begin of each compressed file. We know the
// file size, that means we know number of sectors that means we know the first
// DWORD value in sector position. And if we know encrypted and decrypted value,
// we can find the decryption key !!!
//
// hf            - MPQ file handle
// SectorOffsets - DWORD array of sector positions
// ch            - Decrypted value of the first sector pos

DWORD DetectFileKeyBySectorSize(DWORD * SectorOffsets, DWORD decrypted)
{
    DWORD saveKey1;
    DWORD temp = *SectorOffsets ^ decrypted;    // temp = seed1 + seed2
    temp -= 0xEEEEEEEE;                 // temp = seed1 + StormBuffer[0x400 + (seed1 & 0xFF)]

    for(int i = 0; i < 0x100; i++)      // Try all 255 possibilities
    {
        DWORD seed1;
        DWORD seed2 = 0xEEEEEEEE;
        DWORD ch;

        // Try the first DWORD (We exactly know the value)
        seed1  = temp - StormBuffer[0x400 + i];
        seed2 += StormBuffer[0x400 + (seed1 & 0xFF)];
        ch     = SectorOffsets[0] ^ (seed1 + seed2);

        if(ch != decrypted)
            continue;

        // Add 1 because we are decrypting sector positions
        saveKey1 = seed1 + 1;

        // If OK, continue and test the second value. We don't know exactly the value,
        // but we know that the second one has lower 16 bits set to zero
        // (no compressed sector is larger than 0xFFFF bytes)
        seed1  = ((~seed1 << 0x15) + 0x11111111) | (seed1 >> 0x0B);
        seed2  = ch + seed2 + (seed2 << 5) + 3;

        seed2 += StormBuffer[0x400 + (seed1 & 0xFF)];
        ch     = SectorOffsets[1] ^ (seed1 + seed2);

        if((ch & 0xFFFF0000) == 0)
            return saveKey1;
    }
    return 0;
}

// Function tries to detect file encryption key. It expectes at least two uncompressed bytes
DWORD DetectFileKeyByKnownContent(VOID * pvFileContent, UINT nDwords, ...)
{
    DWORD * pdwContent = (DWORD *)pvFileContent;
    va_list argList;
    DWORD dwDecrypted[0x10];
    DWORD saveKey1;
    DWORD dwTemp;
    DWORD i, j;
    
    // We need at least two DWORDS to detect the file key
    if(nDwords < 0x02 || nDwords > 0x10)
        return 0;
    
    va_start(argList, nDwords);
    for(i = 0; i < nDwords; i++)
        dwDecrypted[i] = va_arg(argList, DWORD);
    va_end(argList);
    
    dwTemp = (*pdwContent ^ dwDecrypted[0]) - 0xEEEEEEEE;
    for(i = 0; i < 0x100; i++)      // Try all 256 possibilities
    {
        DWORD seed1;
        DWORD seed2 = 0xEEEEEEEE;
        DWORD ch;

        // Try the first DWORD
        seed1  = dwTemp - StormBuffer[0x400 + i];
        seed2 += StormBuffer[0x400 + (seed1 & 0xFF)];
        ch     = pdwContent[0] ^ (seed1 + seed2);

        if(ch != dwDecrypted[0])
            continue;

        saveKey1 = seed1;

        // If OK, continue and test all bytes.
        for(j = 1; j < nDwords; j++)
        {
            seed1  = ((~seed1 << 0x15) + 0x11111111) | (seed1 >> 0x0B);
            seed2  = ch + seed2 + (seed2 << 5) + 3;

            seed2 += StormBuffer[0x400 + (seed1 & 0xFF)];
            ch     = pdwContent[j] ^ (seed1 + seed2);

            if(ch == dwDecrypted[j] && j == nDwords - 1)
                return saveKey1;
        }
    }
    return 0;
}

DWORD DetectFileKeyByContent(VOID * pvFileContent, DWORD dwFileSize)
{
    DWORD dwFileKey;

    // Try to break the file encryption key as if it was a WAVE file
    if(dwFileSize >= 0x0C)
    {
        dwFileKey = DetectFileKeyByKnownContent(pvFileContent, 3, 0x46464952, dwFileSize - 8, 0x45564157);
        if(dwFileKey != 0)
            return dwFileKey;
    }

    // Try to break the encryption key as if it was an EXE file
    if(dwFileSize > 0x40)
    {
        dwFileKey = DetectFileKeyByKnownContent(pvFileContent, 2, 0x00905A4D, 0x00000003);
        if(dwFileKey != 0)
            return dwFileKey;
    }

    // Try to break the encryption key as if it was a XML file
    if(dwFileSize > 0x04)
    {
        dwFileKey = DetectFileKeyByKnownContent(pvFileContent, 2, 0x6D783F3C, 0x6576206C);
        if(dwFileKey != 0)
            return dwFileKey;
    }

    // Not detected, sorry
    return 0;
}

//-----------------------------------------------------------------------------
// Handle validation functions

BOOL IsValidMpqHandle(TMPQArchive * ha)
{
    if(ha == NULL || IsBadReadPtr(ha, sizeof(TMPQArchive)))
        return FALSE;
    if(ha->pHeader == NULL || IsBadReadPtr(ha->pHeader, MPQ_HEADER_SIZE_V2))
        return FALSE;
    
    return (ha->pHeader->dwID == ID_MPQ);
}

BOOL IsValidFileHandle(TMPQFile * hf)
{
    if(hf == NULL || IsBadReadPtr(hf, sizeof(TMPQFile)))
        return FALSE;

    if(hf->hFile != INVALID_HANDLE_VALUE)
        return TRUE;

    return IsValidMpqHandle(hf->ha);
}

//-----------------------------------------------------------------------------
// Hash table and block table manioulation

#define GET_NEXT_HASH_ENTRY(pStartHash, pHash, pHashEnd)  \
        if(++pHash >= pHashEnd)                           \
            pHash = ha->pHashTable;                       \
        if(pHash == pStartHash)                           \
            break;

// Retrieves the first hash entry for the given file.
// Every locale version of a file has its own hash entry
TMPQHash * GetFirstHashEntry(TMPQArchive * ha, const char * szFileName)
{
    TMPQHash * pStartHash;                  // File hash entry (start)
    TMPQHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
    TMPQHash * pHash;                       // File hash entry (current)
    DWORD dwIndex = HashString(szFileName, MPQ_HASH_TABLE_OFFSET);
    DWORD dwName1 = HashString(szFileName, MPQ_HASH_NAME_A);
    DWORD dwName2 = HashString(szFileName, MPQ_HASH_NAME_B);

    // Get the first possible has entry that might be the one
    pStartHash = pHash = ha->pHashTable + (dwIndex & (ha->pHeader->dwHashTableSize - 1));

    // There might be deleted entries in the hash table prior to our desired entry.
    while(pHash->dwBlockIndex != HASH_ENTRY_FREE)
    {
        // If the entry agrees, we found it.
        if(pHash->dwName1 == dwName1 && pHash->dwName2 == dwName2 && pHash->dwBlockIndex < ha->pHeader->dwBlockTableSize)
            return pHash;

        // Move to the next hash entry. Stop searching
        // if we got reached the original hash entry
        GET_NEXT_HASH_ENTRY(pStartHash, pHash, pHashEnd)
    }

    // The apropriate hash entry was not found
    return NULL;
}

TMPQHash * GetNextHashEntry(TMPQArchive * ha, TMPQHash * pFirstHash, TMPQHash * pPrevHash)
{
    TMPQHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
    TMPQHash * pHash = pPrevHash;
    DWORD dwName1 = pPrevHash->dwName1;
    DWORD dwName2 = pPrevHash->dwName2;

    // Now go for any next entry that follows the pPrevHash,
    // until either free hash entry was found, or the start entry was reached
    for(;;)
    {
        // Move to the next hash entry. Stop searching
        // if we got reached the original hash entry
        GET_NEXT_HASH_ENTRY(pFirstHash, pHash, pHashEnd)

        // If the entry is a free entry, stop search
        if(pHash->dwBlockIndex == HASH_ENTRY_FREE)
            break;

        // If the entry is not free and the name agrees, we found it
        if(pHash->dwName1 == dwName1 && pHash->dwName2 == dwName2 && pHash->dwBlockIndex < ha->pHeader->dwBlockTableSize)
            return pHash;
    }

    // No next entry
    return NULL;
}

// Returns a hash table entry in the following order:
// 1) A hash table entry with the preferred locale
// 2) A hash table entry with the neutral locale
// 3) NULL
TMPQHash * GetHashEntryLocale(TMPQArchive * ha, const char * szFileName, LCID lcLocale)
{
    TMPQHash * pHashNeutral = NULL;
    TMPQHash * pFirstHash = GetFirstHashEntry(ha, szFileName);
    TMPQHash * pHash = pFirstHash;

    // Parse the found hashes
    while(pHash != NULL)
    {
        // If the locales match, return it
        if(pHash->lcLocale == lcLocale)
            return pHash;
        
        // If we found neutral hash, remember it
        if(pHash->lcLocale == 0)
            pHashNeutral = pHash;

        // Get the next hash entry for that file
        pHash = GetNextHashEntry(ha, pFirstHash, pHash); 
    }

    // At the end, return neutral hash (if found), otherwise NULL
    return pHashNeutral;
}

// Returns a hash table entry in the following order:
// 1) A hash table entry with the preferred locale
// 2) NULL
TMPQHash * GetHashEntryExact(TMPQArchive * ha, const char * szFileName, LCID lcLocale)
{
    TMPQHash * pFirstHash = GetFirstHashEntry(ha, szFileName);
    TMPQHash * pHash = pFirstHash;

    // Parse the found hashes
    while(pHash != NULL)
    {
        // If the locales match, return it
        if(pHash->lcLocale == lcLocale)
            return pHash;
        
        // Get the next hash entry for that file
        pHash = GetNextHashEntry(ha, pFirstHash, pHash); 
    }

    // Not found
    return NULL;
}

// Returns a hash table entry in the following order:
// 1) A hash table entry with the neutral locale
// 2) A hash table entry with any other locale
// 3) NULL
TMPQHash * GetHashEntryAny(TMPQArchive * ha, const char * szFileName)
{
    TMPQHash * pHashNeutral = NULL;
    TMPQHash * pFirstHash = GetFirstHashEntry(ha, szFileName);
    TMPQHash * pHashAny = NULL;
    TMPQHash * pHash = pFirstHash;

    // Parse the found hashes
    while(pHash != NULL)
    {
        // If we found neutral hash, remember it
        if(pHash->lcLocale == 0)
            pHashNeutral = pHash;
        if(pHashAny == NULL)
            pHashAny = pHash;

        // Get the next hash entry for that file
        pHash = GetNextHashEntry(ha, pFirstHash, pHash); 
    }

    // At the end, return neutral hash (if found), otherwise NULL
    return (pHashNeutral != NULL) ? pHashNeutral : pHashAny;
}

TMPQHash * GetHashEntryByIndex(TMPQArchive * ha, DWORD dwFileIndex)
{
    TMPQHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
    TMPQHash * pHash;

    for(pHash = ha->pHashTable; pHash != pHashEnd; pHash++)
    {
        // If the file index matches, return the hash
        if(pHash->dwBlockIndex == dwFileIndex)
            return pHash;
    }

    // Not found, sorry
    return NULL;
}

// Finds the nearest free hash entry for a file
TMPQHash * FindFreeHashEntry(TMPQHash * pHashTable, DWORD dwHashTableSize, const char * szFileName)
{
    TMPQHash * pStartHash;                  // Starting point of the search
    TMPQHash * pHashEnd = pHashTable + dwHashTableSize;
    TMPQHash * pHash;
    DWORD dwIndex = HashString(szFileName, MPQ_HASH_TABLE_OFFSET);
    DWORD dwName1 = HashString(szFileName, MPQ_HASH_NAME_A);
    DWORD dwName2 = HashString(szFileName, MPQ_HASH_NAME_B);

    // Save the starting hash position
    pStartHash = pHash = pHashTable + (dwIndex & (dwHashTableSize - 1));

    // Look for the first free or deleted hash entry.
    while(pHash->dwBlockIndex < HASH_ENTRY_DELETED)
    {
        if(++pHash >= pHashEnd)
            pHash = pHashTable;
        if(pHash == pStartHash)
            return NULL;
    }

    // Fill the hash entry with the informations about the file name
    pHash->dwName1   = dwName1;
    pHash->dwName2   = dwName2;
    pHash->lcLocale  = 0;
    pHash->wPlatform = wPlatform;
    pHash->dwBlockIndex = 0xFFFFFFFF;
    return pHash;
}

// Finds the nearest free hash entry for a file
TMPQHash * FindFreeHashEntry(TMPQArchive * ha, const char * szFileName)
{
    TMPQHash * pHash;
    DWORD dwBlockIndex = ha->pHeader->dwBlockTableSize;
    DWORD dwIndex;

    pHash = FindFreeHashEntry(ha->pHashTable, ha->pHeader->dwHashTableSize, szFileName);
    if(pHash != NULL)
    {
        // Now we have to find a free block entry
        for(dwIndex = 0; dwIndex < ha->pHeader->dwBlockTableSize; dwIndex++)
        {
            TMPQBlock * pBlock = ha->pBlockTable + dwIndex;

            if((pBlock->dwFlags & MPQ_FILE_EXISTS) == 0)
            {
                dwBlockIndex = dwIndex;
                break;
            }
        }

        // Put the block index to the hash table
        pHash->dwBlockIndex = dwBlockIndex;
    }

    return pHash;
}

// Finds a free space in the MPQ where to store next data
// The free space begins beyond the file that is stored at the fuhrtest
// position in the MPQ.
DWORD FindFreeMpqSpace(TMPQArchive * ha, PLARGE_INTEGER pMpqPos)
{
    LARGE_INTEGER TempPos;                  // For file position in MPQ
    LARGE_INTEGER MpqPos;                   // Free space position, relative to MPQ header
    TMPQBlockEx * pBlockEx = ha->pExtBlockTable;;
    TMPQBlock * pFreeBlock = NULL;
    TMPQBlock * pBlockEnd = ha->pBlockTable + ha->pHeader->dwBlockTableSize;
    TMPQBlock * pBlock = ha->pBlockTable;

    // The initial store position is after MPQ header
    MpqPos.QuadPart = ha->pHeader->dwHeaderSize;

    // Parse the entire block table
    while(pBlock < pBlockEnd)
    {
        // Only take existing files
        if(pBlock->dwFlags & MPQ_FILE_EXISTS)
        {
            // If the end of the file is bigger than current MPQ table pos, update it
            TempPos.HighPart = pBlockEx->wFilePosHigh;
            TempPos.LowPart = pBlock->dwFilePos;
            TempPos.QuadPart += pBlock->dwCSize;
            if(TempPos.QuadPart > MpqPos.QuadPart)
                MpqPos.QuadPart = TempPos.QuadPart;
        }
        else
        {
            if(pFreeBlock == NULL)
                pFreeBlock = pBlock;
        }

        // Move to the next block
        pBlockEx++;
        pBlock++;
    }

    // Give the free space position to the caller
    if(pMpqPos != NULL)
        *pMpqPos = MpqPos;

    // If we haven't found a free entry in the middle of the block table,
    // we have to increase the size of the block table. 
    if(pFreeBlock == NULL)
    {
        // Don't allow expanding block table if it's bigger than hash table size
        // This scenario will only happen on MPQs with some bogus entries
        // in the block table, which look like a file but don't have 
        // corresponding hash table entry.
        if(ha->pHeader->dwBlockTableSize >= ha->dwBlockTableMax)
            return 0xFFFFFFFF;

        // Take the entry past the current block table end.
        // DON'T increase size of the block table now, because
        // the caller might call this function without intend to add a file.
        pFreeBlock = pBlock;
    }

    // Return the index of the found free entry
    return (DWORD)(pFreeBlock - ha->pBlockTable);
}

//-----------------------------------------------------------------------------
// Common functions - MPQ File

// This function recalculates raw position of hash table,
// block table and extended block table.
static void CalculateTablePositions(TMPQArchive * ha)
{
    LARGE_INTEGER TablePos;                 // A table position, relative to the begin of the MPQ

    // Hash table position is calculated as position beyond
    // the latest stored file
    FindFreeMpqSpace(ha, &TablePos);
    ha->HashTablePos.QuadPart = ha->MpqPos.QuadPart + TablePos.QuadPart;

    // Update the hash table position in the MPQ header
    ha->pHeader->dwHashTablePos = TablePos.LowPart;
    ha->pHeader->wHashTablePosHigh = (USHORT)TablePos.HighPart;

    // Block table follows immediately after the hash table
    TablePos.QuadPart += ha->pHeader->dwHashTableSize * sizeof(TMPQHash);
    ha->BlockTablePos.QuadPart = ha->MpqPos.QuadPart + TablePos.QuadPart;

    // Update block table position in the MPQ header
    ha->pHeader->dwBlockTablePos = TablePos.LowPart;
    ha->pHeader->wBlockTablePosHigh = (USHORT)TablePos.HighPart;

    // Extended block table follows the old block table
    // Note that we will only use extended block table
    // if the current position is beyond 4 GB. 
    TablePos.QuadPart += ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock);
    if(TablePos.HighPart != 0)
    {
        ha->ExtBlockTablePos.QuadPart = ha->MpqPos.QuadPart + TablePos.QuadPart;
        ha->pHeader->ExtBlockTablePos = TablePos;

        TablePos.QuadPart += ha->pHeader->dwBlockTableSize * sizeof(TMPQBlockEx);
    }
    else
    {
        ha->pHeader->ExtBlockTablePos.QuadPart = 0;
        ha->ExtBlockTablePos.QuadPart = 0;
    }

    // Update archive size in the header (only valid for MPQ v1)
    ha->pHeader->dwArchiveSize = (TablePos.HighPart == 0) ? TablePos.LowPart : 0;
}

TMPQFile * CreateMpqFile(TMPQArchive * ha, const char * szFileName)
{
    TMPQFile * hf;
    size_t nSize = sizeof(TMPQFile) + strlen(szFileName);

    // Allocate space for TMPQFile
    hf = (TMPQFile *)ALLOCMEM(BYTE, nSize);
    if(hf != NULL)
    {
        // Fill the file structure
        memset(hf, 0, nSize);
        hf->ha = ha;
        hf->hFile = INVALID_HANDLE_VALUE;
        strcpy(hf->szFileName, szFileName);
    }

    return hf;
}

// Allocates sector buffer and sector offset table
int AllocateSectorBuffer(TMPQFile * hf)
{
    TMPQArchive * ha = hf->ha;

    // Caller of AllocateSectorBuffer must ensure these
    assert(hf->pbFileSector == NULL);
    assert(hf->pBlock != NULL);
    assert(hf->ha != NULL);

    // Determine the file sector size and allocate buffer for it
    hf->dwSectorSize = (hf->pBlock->dwFlags & MPQ_FILE_SINGLE_UNIT) ? hf->pBlock->dwFSize : ha->dwSectorSize;
    hf->pbFileSector = ALLOCMEM(BYTE, hf->dwSectorSize);
    hf->dwSectorOffs = SFILE_INVALID_POS;

    // Return result
    return (hf->pbFileSector != NULL) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

// Allocates sector offset table
int AllocateSectorOffsets(TMPQFile * hf, bool bLoadFromFile)
{
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    DWORD dwArraySize;
    DWORD dwBytesRead = 0;

    // Caller of AllocateSectorOffsets must ensure these
    assert(hf->SectorOffsets == NULL);
    assert(hf->pBlock != NULL);
    assert(hf->ha != NULL);

    // If the file is stored as single unit, just set number of sectors to 1
    if(pBlock->dwFlags & MPQ_FILE_SINGLE_UNIT)
    {
        hf->dwDataSectors = 1;
        hf->dwSectorCount = 1;
        return ERROR_SUCCESS;
    }

    // Calculate the number of data sectors
    hf->dwDataSectors = (pBlock->dwFSize / hf->dwSectorSize);
    if(pBlock->dwFSize % hf->dwSectorSize)
        hf->dwDataSectors++;

    // Calculate the number of file sectors
    // Note: I've seen corrupted MPQs that had MPQ_FILE_SECTOR_CRC flag absent,
    // but there was one extra sector offset, with the same value as the last one,
    // which corresponds to files with MPQ_FILE_SECTOR_CRC set, but with sector
    // checksums not present. StormLib doesn't handle such MPQs in any special way.
    // Compacting archive on such MPQs fails.
    hf->dwSectorCount = (pBlock->dwFSize / hf->dwSectorSize) + 1;
    if(pBlock->dwFSize % hf->dwSectorSize)
        hf->dwSectorCount++;
    if(pBlock->dwFlags & MPQ_FILE_SECTOR_CRC)
        hf->dwSectorCount++;

    // Only allocate and load the table if the file is compressed
    if(pBlock->dwFlags & MPQ_FILE_COMPRESSED)
    {
        // Allocate the sector offset table
        hf->SectorOffsets = ALLOCMEM(DWORD, hf->dwSectorCount);
        if(hf->SectorOffsets == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Calculate the size of the bytes to be read
        dwArraySize = hf->dwSectorCount * sizeof(DWORD);

        // Only read from the file if we are supposed to do so
        if(bLoadFromFile)
        {
            // Move file pointer to the begin of the file in the MPQ
            SetFilePointer(ha->hFile, hf->RawFilePos.LowPart, &hf->RawFilePos.HighPart, FILE_BEGIN);

            // Load the sector positions
            ReadFile(ha->hFile, hf->SectorOffsets, dwArraySize, &dwBytesRead, NULL);
            BSWAP_ARRAY32_UNSIGNED(hf->SectorOffsets, dwArraySize);
            if(dwBytesRead != dwArraySize)
            {
                // Free the sector offsets
                FREEMEM(hf->SectorOffsets);
                hf->SectorOffsets = NULL;
                return ERROR_FILE_CORRUPT;
            }

            // Decrypt loaded sector positions if necessary
            if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
            {
                // If we don't know the file key, try to find it.
                if(hf->dwFileKey == 0)
                {
                    hf->dwFileKey = DetectFileKeyBySectorSize(hf->SectorOffsets, dwBytesRead);
                    if(hf->dwFileKey == 0)
                    {
                        FREEMEM(hf->SectorOffsets);
                        hf->SectorOffsets = NULL;
                        return ERROR_UNKNOWN_FILE_KEY;
                    }
                }

                // Decrypt sector positions
                DecryptMpqBlock(hf->SectorOffsets, dwBytesRead, hf->dwFileKey - 1);

                //
                // Check if the sector positions are correctly decrypted
                // I saw a protector who puts negative offset into the sector offset table.
                // Because there are always at least 2 sector positions, we can check their difference
                //

                if((hf->SectorOffsets[1] - hf->SectorOffsets[0]) > ha->dwSectorSize)
                {
                    FREEMEM(hf->SectorOffsets);
                    hf->SectorOffsets = NULL;
                    return ERROR_FILE_CORRUPT;
                }
            }
        }
        else
        {
            memset(hf->SectorOffsets, 0, dwArraySize);
            hf->SectorOffsets[0] = dwArraySize;
        }
    }

    return ERROR_SUCCESS;
}

int AllocateSectorChecksums(TMPQFile * hf, bool bLoadFromFile)
{
    LARGE_INTEGER RawFilePos;
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    LPBYTE pbCompressed = NULL;
    DWORD dwCompressedSize;
    DWORD dwTransferred;
    DWORD dwCrcOffset;                      // Offset of the CRC table, relative to file offset in the MPQ
    DWORD dwCrcSize;

    // Caller of AllocateSectorChecksums must ensure these
    assert(hf->SectorChksums == NULL);
    assert(hf->SectorOffsets != NULL);
    assert(hf->pBlock != NULL);
    assert(hf->ha != NULL);

    // Single unit files don't have sector checksums
    if(pBlock->dwFlags & MPQ_FILE_SINGLE_UNIT)
        return ERROR_SUCCESS;

    // Caller must ensure that we are only called when we have sector checksums
    assert(pBlock->dwFlags & MPQ_FILE_SECTOR_CRC);

    // If we only have to allocate the buffer, do it
    if(bLoadFromFile == false)
    {
        // Allocate buffer for sector checksums
        hf->SectorChksums = ALLOCMEM(DWORD, hf->dwDataSectors);
        if(hf->SectorChksums == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        memset(hf->SectorChksums, 0, hf->dwDataSectors * sizeof(DWORD));
        return ERROR_SUCCESS;
    }

    // Check size of the checksums. If zero, there aren't any
    dwCompressedSize = hf->SectorOffsets[hf->dwDataSectors + 1] - hf->SectorOffsets[hf->dwDataSectors];
    if(dwCompressedSize == 0)
        return ERROR_SUCCESS;

    // Allocate buffer for sector CRCs
    hf->SectorChksums = ALLOCMEM(DWORD, hf->dwDataSectors);
    if(hf->SectorChksums == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Calculate offset of the CRC table
    dwCrcOffset = hf->SectorOffsets[hf->dwDataSectors];
    CalculateRawSectorOffset(RawFilePos, hf, dwCrcOffset); 
    SetFilePointer(ha->hFile, RawFilePos.LowPart, &RawFilePos.HighPart, FILE_BEGIN);

    // Is the sector CRCs really compressed ?
    dwCrcSize = hf->dwDataSectors * sizeof(DWORD);
    if(dwCompressedSize < dwCrcSize)
    {
        int cbOutBuffer = (int)dwCrcSize;
        int nError = ERROR_FILE_CORRUPT;

        // Allocate extra buffer for compressed data
        pbCompressed = ALLOCMEM(BYTE, dwCompressedSize);
        if(pbCompressed == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Read the compressed data
        ReadFile(ha->hFile, pbCompressed, dwCompressedSize, &dwTransferred, NULL);
        if(dwTransferred == dwCompressedSize)
        {
            // Decompress the data
            if(SCompDecompress((char *)hf->SectorChksums, &cbOutBuffer, (char *)pbCompressed, dwCompressedSize))
                nError = ERROR_SUCCESS;
        }

        FREEMEM(pbCompressed);
        return nError;
    }
    else
    {
        // Just read the data as-is
        ReadFile(ha->hFile, hf->SectorChksums, dwCrcSize, &dwTransferred, NULL);
        return (dwTransferred == dwCrcSize) ? ERROR_SUCCESS : ERROR_FILE_CORRUPT;
    }
}

void CalculateRawSectorOffset(
    LARGE_INTEGER & RawFilePos, 
    TMPQFile * hf,
    DWORD dwSectorOffset)
{
    //
    // Some MPQ protectors place the sector offset table after the actual file data.
    // Sector offsets in the sector offset table are negative. When added
    // to MPQ file offset from the block table entry, the result is a correct
    // position of the file data in the MPQ.
    //
    // The position of sector table must be always within the MPQ, however.
    // When a negative sector offset is found, we make sure that we make the addition
    // just in 32-bits, and then add the MPQ offset.
    //

    if(dwSectorOffset & 0x80000000)
    {
        dwSectorOffset += hf->pBlock->dwFilePos;
        RawFilePos.QuadPart = hf->ha->MpqPos.QuadPart + dwSectorOffset;
    }
    else
    {
        RawFilePos.QuadPart = hf->RawFilePos.QuadPart + dwSectorOffset;
    }
}

int WriteSectorOffsets(TMPQFile * hf)
{
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    DWORD dwSectorPosLen = hf->dwSectorCount * sizeof(DWORD);
    DWORD dwTransferred;

    // The caller must make sure that this function is only called
    // when the following is true.
    assert(hf->pBlock->dwFlags & MPQ_FILE_COMPRESSED);
    assert(hf->SectorOffsets != NULL);

    // If file is encrypted, sector positions are also encrypted
    if(pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
    {
        BSWAP_ARRAY32_UNSIGNED(hf->SectorOffsets, dwSectorPosLen);
        EncryptMpqBlock(hf->SectorOffsets, dwSectorPosLen, hf->dwFileKey - 1);
        BSWAP_ARRAY32_UNSIGNED(hf->SectorOffsets, dwSectorPosLen);
    }
    
    // Set the position back to the sector offset table
    SetFilePointer(ha->hFile, hf->RawFilePos.LowPart, &hf->RawFilePos.HighPart, FILE_BEGIN);
    
    // Write sector offsets to the archive
    BSWAP_ARRAY32_UNSIGNED(hf->SectorOffsets, dwSectorPosLen);
    WriteFile(ha->hFile, hf->SectorOffsets, dwSectorPosLen, &dwTransferred, NULL);
    return (dwTransferred == dwSectorPosLen) ? ERROR_SUCCESS : ERROR_DISK_FULL;
}


int WriteSectorChecksums(TMPQFile * hf)
{
    LARGE_INTEGER RawFilePos;
    TMPQArchive * ha = hf->ha;
    TMPQBlock * pBlock = hf->pBlock;
    LPBYTE pbCompressed;
    DWORD dwCompressedSize = 0;
    DWORD dwTransferred;
    DWORD dwCrcSize;
    int nOutSize;
    int nError = ERROR_SUCCESS;

    // The caller must make sure that this function is only called
    // when the following is true.
    assert(hf->pBlock->dwFlags & MPQ_FILE_SECTOR_CRC);
    assert(hf->SectorOffsets != NULL);
    assert(hf->SectorChksums != NULL);

    // Calculate size of the checksum array
    dwCrcSize = hf->dwDataSectors * sizeof(DWORD);

    // Allocate buffer for compressed sector CRCs.
    pbCompressed = ALLOCMEM(BYTE, dwCrcSize);
    if(pbCompressed == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Perform the compression
    nOutSize = (int)dwCrcSize;
    SCompCompress((char *)pbCompressed, &nOutSize, (char *)hf->SectorChksums, (int)dwCrcSize, MPQ_COMPRESSION_ZLIB, 0, 0);
    dwCompressedSize = (DWORD)nOutSize;

    // Write the sector CRCs to the archive
    RawFilePos.QuadPart = hf->RawFilePos.QuadPart + hf->SectorOffsets[hf->dwSectorCount - 2];
    SetFilePointer(ha->hFile, RawFilePos.LowPart, &RawFilePos.HighPart, FILE_BEGIN);
    WriteFile(ha->hFile, pbCompressed, dwCompressedSize, &dwTransferred, NULL);         
    if(dwTransferred != dwCompressedSize)
        nError = ERROR_DISK_FULL;

    // Store the sector CRCs 
    hf->SectorOffsets[hf->dwSectorCount - 1] = hf->SectorOffsets[hf->dwSectorCount - 2] + dwCompressedSize;
    pBlock->dwCSize += dwCompressedSize;
    FREEMEM(pbCompressed);
    return nError;
}

// Frees the structure for MPQ file
void FreeMPQFile(TMPQFile *& hf)
{
    if(hf != NULL)
    {
        if(hf->hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hf->hFile);
        if(hf->SectorOffsets != NULL)
            FREEMEM(hf->SectorOffsets);
        if(hf->SectorChksums != NULL)
            FREEMEM(hf->SectorChksums);
        if(hf->pbFileSector != NULL)
            FREEMEM(hf->pbFileSector);
        FREEMEM(hf);
        hf = NULL;
    }
}

// Saves MPQ header, hash table, block table and extended block table.
int SaveMPQTables(TMPQArchive * ha)
{
    BYTE * pbBuffer = NULL;
    DWORD dwBytes;
    DWORD dwWritten;
    DWORD dwBuffSize = STORMLIB_MAX(ha->pHeader->dwHashTableSize, ha->pHeader->dwBlockTableSize);
    int   nError = ERROR_SUCCESS;

    // Calculate positions of the MPQ tables.
    // This sets HashTablePos, BlockTablePos and ExtBlockTablePos,
    // as well as the values in the MPQ header
    CalculateTablePositions(ha);

    // Allocate temporary buffer for tables encryption
    pbBuffer = ALLOCMEM(BYTE, sizeof(TMPQHash) * dwBuffSize);
    if(pbBuffer == NULL)
        nError = ERROR_NOT_ENOUGH_MEMORY;

    // Write the MPQ Header
    if(nError == ERROR_SUCCESS)
    {
        // Write the MPQ header
        SetFilePointer(ha->hFile, ha->MpqPos.LowPart, &ha->MpqPos.HighPart, FILE_BEGIN);

        // Convert to little endian for file save
        BSWAP_TMPQHEADER(ha->pHeader);
        WriteFile(ha->hFile, ha->pHeader, ha->pHeader->dwHeaderSize, &dwWritten, NULL);
        BSWAP_TMPQHEADER(ha->pHeader);

        if(dwWritten == ha->pHeader->dwHeaderSize)
            ha->dwFlags &= ~MPQ_FLAG_NO_HEADER;
        else
            nError = ERROR_DISK_FULL;
    }

    // Write the hash table
    if(nError == ERROR_SUCCESS)
    {
        // Copy the hash table to temporary buffer
        dwBytes = ha->pHeader->dwHashTableSize * sizeof(TMPQHash);
        memcpy(pbBuffer, ha->pHashTable, dwBytes);

        // Convert to little endian for file save
        EncryptMpqTable(pbBuffer, dwBytes, "(hash table)");
        BSWAP_ARRAY32_UNSIGNED(pbBuffer, dwBytes);

        // Set the file pointer to the offset of the hash table and write it
        SetFilePointer(ha->hFile, ha->HashTablePos.LowPart, (PLONG)&ha->HashTablePos.HighPart, FILE_BEGIN);
        WriteFile(ha->hFile, pbBuffer, dwBytes, &dwWritten, NULL);
        if(dwWritten != dwBytes)
            nError = ERROR_DISK_FULL;
    }

    // Write the block table
    if(nError == ERROR_SUCCESS)
    {
        // Copy the block table to temporary buffer
        dwBytes = ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock);
        memcpy(pbBuffer, ha->pBlockTable, dwBytes);

        // Encrypt the block table and write it to the file
        EncryptMpqTable(pbBuffer, dwBytes, "(block table)");
        
        // Convert to little endian for file save
        BSWAP_ARRAY32_UNSIGNED(pbBuffer, dwBytes);
        WriteFile(ha->hFile, pbBuffer, dwBytes, &dwWritten, NULL);
        if(dwWritten != dwBytes)
            nError = ERROR_DISK_FULL;
    }

    // Write the extended block table
    if(nError == ERROR_SUCCESS && ha->pHeader->ExtBlockTablePos.QuadPart != 0)
    {
        // We expect format V2 or newer in this case
        assert(ha->pHeader->wFormatVersion >= MPQ_FORMAT_VERSION_2);

        // Copy the block table to temporary buffer
        dwBytes = ha->pHeader->dwBlockTableSize * sizeof(TMPQBlockEx);
        memcpy(pbBuffer, ha->pExtBlockTable, dwBytes);

        // Convert to little endian for file save
        BSWAP_ARRAY16_UNSIGNED(pbBuffer, dwBytes);
        WriteFile(ha->hFile, pbBuffer, dwBytes, &dwWritten, NULL);
        if(dwWritten != dwBytes)
            nError = ERROR_DISK_FULL;
    }

    // Set end of file here
    if(nError == ERROR_SUCCESS)
    {
        ha->dwFlags &= ~MPQ_FLAG_CHANGED;
        SetEndOfFile(ha->hFile);
    }

    // Cleanup and exit
    if(pbBuffer != NULL)
        FREEMEM(pbBuffer);
    return nError;
}

// Frees the MPQ archive
void FreeMPQArchive(TMPQArchive *& ha)
{
    if(ha != NULL)
    {
        FREEMEM(ha->pBlockTable);
        FREEMEM(ha->pExtBlockTable);
        FREEMEM(ha->pHashTable);
        if(ha->pListFile != NULL)
            SListFileFreeListFile(ha);
        if(ha->pAttributes != NULL)
            FreeMPQAttributes(ha->pAttributes);

        if(ha->hFile != INVALID_HANDLE_VALUE)
            CloseHandle(ha->hFile);
        FREEMEM(ha);
        ha = NULL;
    }
}

const char * GetPlainLocalFileName(const char * szFileName)
{
#ifdef WIN32
    const char * szPlainName = strrchr(szFileName, '\\');
#else
    const char * szPlainName = strrchr(szFileName, '/');
#endif
    return (szPlainName != NULL) ? szPlainName + 1 : szFileName;
}

const char * GetPlainMpqFileName(const char * szFileName)
{
    const char * szPlainName = strrchr(szFileName, '\\');
    return (szPlainName != NULL) ? szPlainName + 1 : szFileName;
}
