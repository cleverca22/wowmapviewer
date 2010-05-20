/*****************************************************************************/
/* SFileOpenFileEx.cpp                    Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* Description :                                                             */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* xx.xx.99  1.00  Lad  The first version of SFileOpenFileEx.cpp             */
/*****************************************************************************/

#define __STORMLIB_SELF__
#include "StormLib.h"
#include "SCommon.h"

/*****************************************************************************/
/* Local functions                                                           */
/*****************************************************************************/

// Is it a pseudo-name ("FileXXXXXXXX.ext") ?
static BOOL IsPseudoFileName(const char * szFileName, DWORD * pdwFileIndex)
{
    const char * szExt = strrchr(szFileName, '.');

    // Must have an extension
    if(szExt != NULL)
    {
        // Length of the name part must be 12 characters
        if((szExt - szFileName) == 12)
        {
            // Must begin with "File"
            if(!_strnicmp(szFileName, "File", 4))
            {
                // Must be 8 digits after "File"
                if(isdigit(szFileName[4]) && isdigit(szFileName[11]))
                {
                    *pdwFileIndex = strtol(szFileName + 4, (char **)&szExt, 10);
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

static BOOL OpenLocalFile(const char * szFileName, HANDLE * phFile)
{
    TMPQFile * hf = NULL;
    HANDLE hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, MPQ_OPEN_EXISTING, 0, NULL);

    if(hFile != INVALID_HANDLE_VALUE)
    {
        // Allocate and initialize file handle
        hf = CreateMpqFile(NULL, szFileName);
        if(hf != NULL)
        {
            hf->hFile = hFile;
            *phFile = hf;
            return TRUE;
        }
        else
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    *phFile = NULL;
    return FALSE;
}

/*****************************************************************************/
/* Public functions                                                          */
/*****************************************************************************/

//-----------------------------------------------------------------------------
// SFileEnumLocales enums all locale versions within MPQ. 
// Functions fills all available language identifiers on a file into the buffer
// pointed by plcLocales. There must be enough entries to copy the localed,
// otherwise the function returns ERROR_INSUFFICIENT_BUFFER.

int WINAPI SFileEnumLocales(
    HANDLE hMpq,
    const char * szFileName,
    LCID * plcLocales,
    DWORD * pdwMaxLocales,
    DWORD dwSearchScope)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TMPQHash * pFirstHash;
    TMPQHash * pHash;
    DWORD dwLocales = 0;

    // Test the parameters
    if(IsValidMpqHandle(ha) == FALSE)
        return ERROR_INVALID_HANDLE;
    if(pdwMaxLocales == NULL)
        return ERROR_INVALID_PARAMETER;
    if(dwSearchScope == SFILE_OPEN_BY_INDEX && (DWORD_PTR)szFileName > ha->pHeader->dwBlockTableSize)
        return ERROR_INVALID_PARAMETER;
    if(dwSearchScope != SFILE_OPEN_BY_INDEX && *szFileName == 0)
        return ERROR_INVALID_PARAMETER;

    // Parse hash table entries for all locales
    if(dwSearchScope == SFILE_OPEN_FROM_MPQ)
    {
        pFirstHash = pHash = GetFirstHashEntry(ha, szFileName);
        while(pHash != NULL)
        {
            dwLocales++;
            pHash = GetNextHashEntry(ha, pFirstHash, pHash);
        }
    }
    else
    {
        // For nameless access, always return 1 locale
        pHash = GetHashEntryByIndex(ha, (DWORD)(DWORD_PTR)szFileName);
        if(pHash != NULL)
            dwLocales++;
    }

    // Test if there is enough space to copy the locales
    if(*pdwMaxLocales < dwLocales)
    {
        *pdwMaxLocales = dwLocales;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    // Now find all locales
    if(dwSearchScope == SFILE_OPEN_FROM_MPQ)
    {
        pFirstHash = pHash = GetFirstHashEntry(ha, szFileName);
        while(pHash != NULL)
        {
            *plcLocales++ = pHash->lcLocale;
            pHash = GetNextHashEntry(ha, pFirstHash, pHash);
        }
    }
    else
    {
        // For nameless access, always return 1 locale
        pHash = GetHashEntryByIndex(ha, (DWORD)(DWORD_PTR)szFileName);
        if(pHash != NULL)
            *plcLocales++ = pHash->lcLocale;
    }

    // Give the caller the total number of found locales
    *pdwMaxLocales = dwLocales;
    return ERROR_SUCCESS;
}

//-----------------------------------------------------------------------------
// SFileHasFile
//
//   hMpq          - Handle of opened MPQ archive
//   szFileName    - Name of file to look for

BOOL WINAPI SFileHasFile(HANDLE hMpq, const char * szFileName)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    int nError = ERROR_SUCCESS;

    if(IsValidMpqHandle(ha) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(*szFileName == 0)
        nError = ERROR_INVALID_PARAMETER;

    // Prepare the file opening
    if(nError == ERROR_SUCCESS)
    {
        if(GetHashEntryLocale(ha, szFileName, lcFileLocale) == NULL)
        {
            nError = ERROR_FILE_NOT_FOUND;
        }
    }

    // Cleanup
    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}


//-----------------------------------------------------------------------------
// SFileOpenFileEx
//
//   hMpq          - Handle of opened MPQ archive
//   szFileName    - Name of file to open
//   dwSearchScope - Where to search
//   phFile        - Pointer to store opened file handle

BOOL WINAPI SFileOpenFileEx(HANDLE hMpq, const char * szFileName, DWORD dwSearchScope, HANDLE * phFile)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TMPQFile    * hf = NULL;
    TMPQHash    * pHash = NULL;         // Hash table index
    TMPQBlock   * pBlock = NULL;        // File block
    TMPQBlockEx * pBlockEx = NULL;
    DWORD dwHashIndex  = 0;             // Hash table index
    DWORD dwBlockIndex = (DWORD)-1;     // Found table index
    size_t nHandleSize = 0;             // Memory space necessary to allocate TMPQHandle
    int nError = ERROR_SUCCESS;

    // Don't accept NULL pointer to file handle
    if(phFile == NULL)
        nError = ERROR_INVALID_PARAMETER;

    // Prepare the file opening
    if(nError == ERROR_SUCCESS)
    {
        switch(dwSearchScope)
        {
            case SFILE_OPEN_FROM_MPQ:
                
                if(IsValidMpqHandle(ha) == FALSE)
                {
                    nError = ERROR_INVALID_HANDLE;
                    break;
                }

                if(szFileName == NULL || *szFileName == 0)
                {
                    nError = ERROR_INVALID_PARAMETER;
                    break;
                }

                // Is it a pseudo-name ("FileXXXXXXXX.ext") ?
                if(!IsPseudoFileName(szFileName, &dwBlockIndex))
                {
                    nHandleSize = sizeof(TMPQFile) + strlen(szFileName);
                    pHash = GetHashEntryLocale(ha, szFileName, lcFileLocale);
                    if(pHash == NULL)
                        nError = ERROR_FILE_NOT_FOUND;
                    break;
                }

                // Set the file name to the file index and fall through
                szFileName = (const char *)(DWORD_PTR)dwBlockIndex;
                dwSearchScope = SFILE_OPEN_BY_INDEX;
                // No break here, fall through.

            case SFILE_OPEN_BY_INDEX:

                if(IsValidMpqHandle(ha) == FALSE)
                {
                    nError = ERROR_INVALID_HANDLE;
                    break;
                }

                // Set handle size to be sizeof(TMPQFile) + length of FileXXXXXXXX.xxx
                nHandleSize = sizeof(TMPQFile) + 20;
                pHash = GetHashEntryByIndex(ha, (DWORD)(DWORD_PTR)szFileName);
                if(pHash == NULL)
                    nError = ERROR_FILE_NOT_FOUND;
                break;

            case SFILE_OPEN_ANY_LOCALE:

                // This open option is reserved for opening MPQ internal listfile.
                // No argument validation. Tries to open file with neutral locale first,
                // then any other available.
                dwSearchScope = SFILE_OPEN_FROM_MPQ;
                nHandleSize = sizeof(TMPQFile) + strlen(szFileName);
                pHash = GetHashEntryAny(ha, szFileName);
                if(pHash == NULL)
                    nError = ERROR_FILE_NOT_FOUND;
                break;

            case SFILE_OPEN_LOCAL_FILE:

                // Just pass it fuhrter, let CreateFile
                // deal with eventual invalid parameters
                return OpenLocalFile(szFileName, phFile); 

            default:

                // Don't accept any other value
                nError = ERROR_INVALID_PARAMETER;
                break;
        }

        // Quick return if something failed
        if(nError != ERROR_SUCCESS)
        {
            SetLastError(nError);
            return FALSE;
        }
    }

    // Get block index from file name and test it
    if(nError == ERROR_SUCCESS)
    {
        dwHashIndex  = (DWORD)(pHash - ha->pHashTable);
        dwBlockIndex = pHash->dwBlockIndex;

        // If index was not found, or is greater than number of files, exit.
        // This also covers the deleted files and free entries
        if(dwBlockIndex > ha->pHeader->dwBlockTableSize)
            nError = ERROR_FILE_NOT_FOUND;
    }

    // Get block and test if the file was not already deleted.
    if(nError == ERROR_SUCCESS)
    {
        // Get both block tables and file position
        pBlockEx = ha->pExtBlockTable + dwBlockIndex;
        pBlock = ha->pBlockTable + dwBlockIndex;

        if((pBlock->dwFlags & MPQ_FILE_EXISTS) == 0)
            nError = ERROR_FILE_NOT_FOUND;
        if(pBlock->dwFlags & ~MPQ_FILE_VALID_FLAGS)
            nError = ERROR_NOT_SUPPORTED;
    }

    // Allocate file handle
    if(nError == ERROR_SUCCESS)
    {
        if((hf = (TMPQFile *)ALLOCMEM(char, nHandleSize)) == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Initialize file handle
    if(nError == ERROR_SUCCESS)
    {
        memset(hf, 0, nHandleSize);
        hf->hFile    = INVALID_HANDLE_VALUE;
        hf->ha       = ha;
        hf->pBlockEx = pBlockEx;
        hf->pBlock   = pBlock;
        hf->pHash    = pHash;
        
        hf->MpqFilePos.HighPart = pBlockEx->wFilePosHigh;
        hf->MpqFilePos.LowPart  = pBlock->dwFilePos;
        hf->RawFilePos.QuadPart = hf->MpqFilePos.QuadPart + ha->MpqPos.QuadPart;

        hf->dwHashIndex  = dwHashIndex;
        hf->dwBlockIndex = dwBlockIndex; 

        // If the MPQ has sector CRC enabled, enable if for the file
        if(ha->dwFlags & MPQ_FLAG_CHECK_SECTOR_CRC)
            hf->bCheckSectorCRCs = TRUE;

        // Decrypt file key. Cannot be used if the file is given by index
        if(dwSearchScope == SFILE_OPEN_FROM_MPQ)
        {
            strcpy(hf->szFileName, szFileName);
            if(hf->pBlock->dwFlags & MPQ_FILE_ENCRYPTED)
            {
                szFileName = GetPlainMpqFileName(szFileName);
                hf->dwFileKey = DecryptFileKey(szFileName);
                if(hf->pBlock->dwFlags & MPQ_FILE_FIX_KEY)
                {
                    hf->dwFileKey = (hf->dwFileKey + hf->pBlock->dwFilePos) ^ hf->pBlock->dwFSize;
                }
            }
        }
        else
        {
            // If the file is encrypted and not compressed, we cannot detect the file key
            if(SFileGetFileName(hf, hf->szFileName) == FALSE)
                nError = GetLastError();
        }
    }

    // Resolve pointers to file's attributes
    if(nError == ERROR_SUCCESS && ha->pAttributes != NULL)
    {
        if(ha->pAttributes->pCrc32 != NULL)
            hf->pCrc32 = ha->pAttributes->pCrc32 + dwBlockIndex;
        if(ha->pAttributes->pFileTime != NULL)
            hf->pFileTime = ha->pAttributes->pFileTime + dwBlockIndex;
        if(ha->pAttributes->pMd5 != NULL)
            hf->pMd5 = ha->pAttributes->pMd5 + dwBlockIndex;
    }

    // Cleanup
    if(nError != ERROR_SUCCESS)
    {
        FreeMPQFile(hf);
        SetLastError(nError);
    }

    *phFile = hf;
    return (nError == ERROR_SUCCESS);
}

//-----------------------------------------------------------------------------
// BOOL WINAPI SFileCloseFile(HANDLE hFile);

BOOL WINAPI SFileCloseFile(HANDLE hFile)
{
    TMPQFile * hf = (TMPQFile *)hFile;
    
    if(IsValidFileHandle(hf) == FALSE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    // Free the structure
    FreeMPQFile(hf);
    return TRUE;
}
