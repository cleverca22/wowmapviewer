/*****************************************************************************/
/* SFileFindFile.cpp                      Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* A module for file searching within MPQs                                   */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 25.03.03  1.00  Lad  The first version of SFileFindFile.cpp               */
/*****************************************************************************/

#define __STORMLIB_SELF__
#include "StormLib.h"
#include "SCommon.h"

//-----------------------------------------------------------------------------
// Defines

#define LISTFILE_CACHE_SIZE 0x1000

//-----------------------------------------------------------------------------
// Local functions

static BOOL IsValidSearchHandle(TMPQSearch * hs)
{
    if(hs == NULL || IsBadReadPtr(hs, sizeof(TMPQSearch)))
        return FALSE;

    if(IsValidMpqHandle(hs->ha) == FALSE)
        return FALSE;

    return TRUE;
}

BOOL CheckWildCard(const char * szString, const char * szWildCard)
{
    const char * szSubString;
    int nSubStringLength;
    int nMatchCount = 0;

    // When the mask is empty, it never matches
    if(szWildCard == NULL || *szWildCard == 0)
        return FALSE;

    // If the wildcard contains just "*", then it always matches
    if(szWildCard[0] == '*' && szWildCard[1] == 0)
        return TRUE;

    // Do normal test
    for(;;)
    {
        // If there is '?' in the wildcard, we skip one char
        while(*szWildCard == '?')
        {
            szWildCard++;
            szString++;
        }

        // If there is '*', means zero or more chars. We have to 
        // find the sequence after '*'
        if(*szWildCard == '*')
        {
            // More stars is equal to one star
            while(*szWildCard == '*' || *szWildCard == '?')
                szWildCard++;

            // If we found end of the wildcard, it's a match
            if(*szWildCard == 0)
                return TRUE;

            // Determine the length of the substring in szWildCard
            szSubString = szWildCard;
            while(*szSubString != 0 && *szSubString != '?' && *szSubString != '*')
                szSubString++;
            nSubStringLength = (int)(szSubString - szWildCard);
            nMatchCount = 0;

            // Now we have to find a substring in szString,
            // that matches the substring in szWildCard
            while(*szString != 0)
            {
                // Calculate match count
                while(nMatchCount < nSubStringLength)
                {
                    if(toupper(szString[nMatchCount]) != toupper(szWildCard[nMatchCount]))
                        break;
                    if(szString[nMatchCount] == 0)
                        break;
                    nMatchCount++;
                }

                // If the match count has reached substring length, we found a match
                if(nMatchCount == nSubStringLength)
                {
                    szWildCard += nMatchCount;
                    szString += nMatchCount;
                    break;
                }

                // No match, move to the next char in szString
                nMatchCount = 0;
                szString++;
            }
        }
        else
        {
            // If we came to the end of the string, compare it to the wildcard
            if(toupper(*szString) != toupper(*szWildCard))
                return FALSE;

            // If we arrived to the end of the string, it's a match
            if(*szString == 0)
                return TRUE;

            // Otherwise, continue in comparing
            szWildCard++;
            szString++;
        }
    }
}

// Performs one MPQ search
static int DoMPQSearch(TMPQSearch * hs, SFILE_FIND_DATA * lpFindFileData)
{
    TMPQArchive * ha = hs->ha;
    TFileNode * pNode = NULL;
    TMPQBlock * pBlock;
    TMPQHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
    TMPQHash * pHash = ha->pHashTable + hs->dwNextIndex;
    DWORD dwHashIndex;
    DWORD dwBlockIndex;

    // Do until a file is found or no more files
    while(pHash < pHashEnd)
    {
        pNode = ha->pListFile[hs->dwNextIndex++];

        // Is this entry occupied ?
        // Note don't check the block index to HASH_ENTRY_DELETED.
        // Various maliciously modified MPQs have garbage here
        if(pHash->dwBlockIndex < ha->pHeader->dwBlockTableSize)
        {
            // Check if the file exists
            pBlock = ha->pBlockTable + pHash->dwBlockIndex;
            if((pBlock->dwFlags & MPQ_FILE_EXISTS) && (pNode != NULL))
            {
                // Check the file name.
                if(CheckWildCard(pNode->szFileName, hs->szSearchMask))
                {
                    // Calculate hash index
                    dwHashIndex = (DWORD)(pHash - ha->pHashTable);
                    dwBlockIndex = pHash->dwBlockIndex;

                    // Fill the found entry
                    lpFindFileData->dwHashIndex  = dwHashIndex;
                    lpFindFileData->dwBlockIndex = pHash->dwBlockIndex;
                    lpFindFileData->dwFileSize   = pBlock->dwFSize;
                    lpFindFileData->dwFileFlags  = pBlock->dwFlags;
                    lpFindFileData->dwCompSize   = pBlock->dwCSize;
                    lpFindFileData->lcLocale     = pHash->lcLocale;

                    // Fill the filetime
                    lpFindFileData->dwFileTimeLo = 0;
                    lpFindFileData->dwFileTimeHi = 0;
                    if(ha->pAttributes != NULL && (ha->pAttributes->dwFlags & MPQ_ATTRIBUTE_FILETIME))
                    {
                        lpFindFileData->dwFileTimeLo = ha->pAttributes->pFileTime[dwBlockIndex].dwFileTimeLow;
                        lpFindFileData->dwFileTimeHi = ha->pAttributes->pFileTime[dwBlockIndex].dwFileTimeHigh;
                    }

                    // Fill the file name and plain file name
                    strcpy(lpFindFileData->cFileName, pNode->szFileName);
                    lpFindFileData->szPlainName = (char *)GetPlainMpqFileName(lpFindFileData->cFileName);

                    // Fill the next entry
                    return ERROR_SUCCESS;
                }
            }
        }

        // Move to the next hash table entry
        pHash++;
    }

    // No more files found, return error
    return ERROR_NO_MORE_FILES;
}

static void FreeMPQSearch(TMPQSearch *& hs)
{
    if(hs != NULL)
    {
        FREEMEM(hs);
        hs = NULL;
    }
}

//-----------------------------------------------------------------------------
// Public functions

HANDLE WINAPI SFileFindFirstFile(HANDLE hMpq, const char * szMask, SFILE_FIND_DATA * lpFindFileData, const char * szListFile)
{
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    TMPQSearch * hs = NULL;             // Search object handle
    size_t nSize  = 0;
    int nError = ERROR_SUCCESS;

    // Check for the valid parameters
    if(IsValidMpqHandle(ha) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(szMask == NULL || lpFindFileData == NULL)
        nError = ERROR_INVALID_PARAMETER;

    // Include the listfile into the MPQ's internal listfile
    // Note that if the listfile name is NULL, do nothing because the
    // internal listfile is always included.
    if(nError == ERROR_SUCCESS && szListFile != NULL && *szListFile != 0)
        nError = SFileAddListFile((HANDLE)ha, szListFile);

    // Allocate the structure for MPQ search
    if(nError == ERROR_SUCCESS)
    {
        nSize = sizeof(TMPQSearch) + strlen(szMask) + 1;
        if((hs = (TMPQSearch *)ALLOCMEM(char, nSize)) == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    // Perform the first search
    if(nError == ERROR_SUCCESS)
    {
        memset(hs, 0, sizeof(TMPQSearch));
        hs->ha = ha;
        strcpy(hs->szSearchMask, szMask);
        nError = DoMPQSearch(hs, lpFindFileData);
    }

    // Cleanup
    if(nError != ERROR_SUCCESS)
    {
        FreeMPQSearch(hs);
        SetLastError(nError);
    }
    
    // Return the result value
    return (HANDLE)hs;
}

BOOL WINAPI SFileFindNextFile(HANDLE hFind, SFILE_FIND_DATA * lpFindFileData)
{
    TMPQSearch * hs = (TMPQSearch *)hFind;
    int nError = ERROR_SUCCESS;

    // Check the parameters
    if(IsValidSearchHandle(hs) == FALSE)
        nError = ERROR_INVALID_HANDLE;
    if(lpFindFileData == NULL)
        nError = ERROR_INVALID_PARAMETER;

    if(nError == ERROR_SUCCESS)
        nError = DoMPQSearch(hs, lpFindFileData);

    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return (nError == ERROR_SUCCESS);
}

BOOL WINAPI SFileFindClose(HANDLE hFind)
{
    TMPQSearch * hs = (TMPQSearch *)hFind;

    // Check the parameters
    if(IsValidSearchHandle(hs) == FALSE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    FreeMPQSearch(hs);
    return TRUE;
}
