/*****************************************************************************/
/* SListFile.cpp                          Copyright (c) Ladislav Zezula 2004 */
/*---------------------------------------------------------------------------*/
/* Description:                                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 12.06.04  1.00  Lad  The first version of SListFile.cpp                   */
/*****************************************************************************/

#define __STORMLIB_SELF__
#include "StormLib.h"
#include "SCommon.h"
#include <assert.h>

//-----------------------------------------------------------------------------
// Listfile entry structure

#define LISTFILE_CACHE_SIZE 0x1000      // Size of one cache element
#define NO_MORE_CHARACTERS 256
#define HASH_TABLE_SIZE    31           // Initial hash table size (should be a prime number)

struct TListFileCache
{
    HANDLE  hFile;                      // Stormlib file handle
    char  * szMask;                     // File mask
    DWORD   dwFileSize;                 // Total size of the cached file
    DWORD   dwBuffSize;                 // File of the cache
    DWORD   dwFilePos;                  // Position of the cache in the file
    BYTE  * pBegin;                     // The begin of the listfile cache
    BYTE  * pPos;
    BYTE  * pEnd;                       // The last character in the file cache

    BYTE Buffer[1];                     // Listfile cache itself
};

//-----------------------------------------------------------------------------
// Local functions (cache)

// Reloads the cache. Returns number of characters
// that has been loaded into the cache.
static DWORD ReloadCache(TListFileCache * pCache)
{
    // Check if there is enough characters in the cache
    // If not, we have to load more
    if(pCache->pPos >= pCache->pEnd)
    {
        // If the cache is already at the end, do nothing more
        if((pCache->dwFilePos + pCache->dwBuffSize) >= pCache->dwFileSize)
            return 0;

        pCache->dwFilePos += pCache->dwBuffSize;
        SFileReadFile(pCache->hFile, pCache->Buffer, pCache->dwBuffSize, &pCache->dwBuffSize, NULL);
        if(pCache->dwBuffSize == 0)
            return 0;

        // Set the buffer pointers
        pCache->pBegin =
        pCache->pPos = &pCache->Buffer[0];
        pCache->pEnd = pCache->pBegin + pCache->dwBuffSize;
    }

    return pCache->dwBuffSize;
}

static size_t ReadLine(TListFileCache * pCache, char * szLine, int nMaxChars)
{
    char * szLineBegin = szLine;
    char * szLineEnd = szLine + nMaxChars - 1;
    
__BeginLoading:

    // Skip newlines, spaces, tabs and another non-printable stuff
    while(pCache->pPos < pCache->pEnd && *pCache->pPos <= 0x20)
        pCache->pPos++;

    // Copy the remaining characters
    while(pCache->pPos < pCache->pEnd && szLine < szLineEnd)
    {
        // If we have found a newline, stop loading
        if(*pCache->pPos == 0x0D || *pCache->pPos == 0x0A)
            break;

        *szLine++ = *pCache->pPos++;
    }

    // If we now need to reload the cache, do it
    if(pCache->pPos == pCache->pEnd)
    {
        if(ReloadCache(pCache) > 0)
            goto __BeginLoading;
    }

    *szLine = 0;
    return (szLine - szLineBegin);
}

static int CompareFileNodes(const void * p1, const void * p2) 
{
    TFileNode * pNode1 = *(TFileNode **)p1;
    TFileNode * pNode2 = *(TFileNode **)p2;

    return _stricmp(pNode1->szFileName, pNode2->szFileName);
}

static TFileNode * CreateFileNode(const char * szFileName)
{
    TFileNode * pNode;
    size_t nLength = strlen(szFileName);

    // Allocate new file node
    pNode = (TFileNode *)ALLOCMEM(char, sizeof(TFileNode) + nLength);
    if(pNode != NULL)
    {
        // Fill the node
        strcpy(pNode->szFileName, szFileName);
        pNode->nLength = nLength;
        pNode->dwRefCount = 0;
    }

    return pNode;
}

//-----------------------------------------------------------------------------
// Local functions (listfile nodes)

// Creates new listfile. The listfile is an array of TListFileNode
// structures. The size of the array is the same like the hash table size,
// the ordering is the same too (listfile node index is the same like
// the index in the MPQ hash table)

int SListFileCreateListFile(TMPQArchive * ha)
{
    DWORD dwItems = ha->pHeader->dwHashTableSize;

    // The listfile should be NULL now
    assert(ha->pListFile == NULL);

    ha->pListFile = ALLOCMEM(TFileNode *, dwItems);
    if(ha->pListFile == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    memset(ha->pListFile, 0, dwItems * sizeof(TFileNode *));
    return ERROR_SUCCESS;
}

// Adds a name into the list of all names. For each locale in the MPQ,
// one entry will be created
// If the file name is already there, does nothing.
int SListFileCreateNodeForAllLocales(TMPQArchive * ha, const char * szFileName)
{
    TFileNode * pNode = NULL;
    TMPQHash * pFirstHash;
    TMPQHash * pHash;
    DWORD dwHashIndex = 0;

    // Look for the first hash table entry for the file
    pFirstHash = pHash = GetFirstHashEntry(ha, szFileName);

    // Go while we found something
    while(pHash != NULL)
    {
        // Compute the hash index
        dwHashIndex = (DWORD)(pHash - ha->pHashTable);

        // Create the file node, if none
        if(ha->pListFile[dwHashIndex] == NULL)
        {
            // Create the listfile node, if doesn't exist yet
            if(pNode == NULL)
                pNode = CreateFileNode(szFileName);

            // Insert the node to the listfile table
            ha->pListFile[dwHashIndex] = pNode;
            pNode->dwRefCount++;
        }

        // Now find the next language version of the file
        pHash = GetNextHashEntry(ha, pFirstHash, pHash);
    }

    return ERROR_SUCCESS;
}

// Adds a filename into the listfile.
// If at least one locale version has the node, we reuse it
int SListFileCreateNode(TMPQArchive * ha, const char * szFileName, TMPQHash * pAddedHash)
{
    TFileNode * pNode = NULL;
    TMPQHash * pFirstHash;
    TMPQHash * pHash;
    DWORD dwHashIndex;

    // Get the first entry that belongs to any lang version of the file
    pFirstHash = pHash = GetFirstHashEntry(ha, szFileName);
    while(pHash != NULL)
    {
        // Does the hash entry have an allocated file name node ?
        dwHashIndex = (DWORD)(pHash - ha->pHashTable);
        if(ha->pListFile[dwHashIndex] != NULL)
        {
            pNode = ha->pListFile[dwHashIndex];
            break;
        }

        // Get to the next hash table entry
        pHash = GetNextHashEntry(ha, pFirstHash, pHash);
    }

    // If none of the nodes has the name yet, create new node
    if(pNode == NULL)
        pNode = CreateFileNode(szFileName);

    // Insert the node to the new position
    ha->pListFile[pAddedHash - ha->pHashTable] = pNode;
    pNode->dwRefCount++;
    return ERROR_SUCCESS;
}

// Removes a filename from the listfile.
// If the name is not there, does nothing
int SListFileRemoveNode(TMPQArchive * ha, TMPQHash * pHash)
{
    TFileNode * pNode = NULL;
    DWORD dwHashIndex;

    // Calculate hash table index
    dwHashIndex = (DWORD)(pHash - ha->pHashTable);

    // Get the node from the index
    pNode = ha->pListFile[dwHashIndex];
    ha->pListFile[dwHashIndex] = NULL;

    // If the node is valid, free it
    if(pNode != NULL)
    {
        pNode->dwRefCount--;
        if(pNode->dwRefCount == 0)
            FREEMEM(pNode);
    }
    return ERROR_SUCCESS;
}

void SListFileFreeListFile(TMPQArchive * ha)
{
    if(ha->pListFile != NULL)
    {
        for(DWORD i = 0; i < ha->pHeader->dwHashTableSize; i++)
        {
            TFileNode * pNode = ha->pListFile[i];

            if(pNode != NULL)
            {
                ha->pListFile[i] = NULL;
                pNode->dwRefCount--;

                if(pNode->dwRefCount == 0)
                    FREEMEM(pNode);
            }
        }

        FREEMEM(ha->pListFile);
        ha->pListFile = NULL;
    }
}

// Saves the whole listfile into the MPQ.
int SListFileSaveToMpq(TMPQArchive * ha)
{
    TFileNode ** SortTable = NULL;
    TFileNode ** ppListFile = ha->pListFile;
    TFileNode * pNode = NULL;
    TMPQHash * pFirstHash;
    TMPQHash * pHashEnd = ha->pHashTable + ha->pHeader->dwHashTableSize;
    TMPQHash * pHash = NULL;
    void * pvAddHandle = NULL;
    DWORD dwFileSize = 0;
    char szNewLine[2] = {0x0D, 0x0A};
    size_t nFileNodes = 0;
    int nError = ERROR_SUCCESS;

    // If no listfile, do nothing
    if(ha->pListFile == NULL)
        return ERROR_SUCCESS;

    // Allocate the table for sorting listfile
    SortTable = ALLOCMEM(TFileNode *, ha->pHeader->dwHashTableSize);
    if(SortTable == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // Calculate the size of (listfile)
    // Parse all file nodes, only add each node once.
    for(pHash = ha->pHashTable; pHash < pHashEnd; pHash++, ppListFile++)
    {
        // If the hash entry belongs to an existing file,
        // get the first occurence of the file in the hash table
        pNode = ppListFile[0];
        if(pHash->dwBlockIndex < ha->pHeader->dwBlockTableSize && pNode != NULL)
        {
            pFirstHash = GetFirstHashEntry(ha, pNode->szFileName);
            if(pFirstHash == pHash)
            {
                SortTable[nFileNodes++] = pNode;
                dwFileSize += (DWORD)(pNode->nLength + 2);
            }
        }
    }

    // Sort the table
    qsort(SortTable, nFileNodes, sizeof(TFileNode *), CompareFileNodes);

    // Create the listfile in the MPQ
    nError = SFileAddFile_Init(ha, LISTFILE_NAME,
                                   NULL,
                                   dwFileSize,
                                   LANG_NEUTRAL,
                                   MPQ_FILE_ENCRYPTED | MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING,
                                  &pvAddHandle);

    // Add all file names
    if(nError == ERROR_SUCCESS)
    {
        // Each name is followed by newline ("\x0D\x0A")
        for(size_t i = 0; i < nFileNodes; i++)
        {
            pNode = SortTable[i];

            nError = SFileAddFile_Write(pvAddHandle, pNode->szFileName, (DWORD)pNode->nLength, MPQ_COMPRESSION_ZLIB);
            if(nError != ERROR_SUCCESS)
                break;

            nError = SFileAddFile_Write(pvAddHandle, szNewLine, sizeof(szNewLine), MPQ_COMPRESSION_ZLIB);
            if(nError != ERROR_SUCCESS)
                break;
        }
    }


    // Finalize the file in the MPQ
    if(pvAddHandle != NULL)
        SFileAddFile_Finish(pvAddHandle, nError);
    if(SortTable != NULL)
        FREEMEM(SortTable);
    return nError;
}

//-----------------------------------------------------------------------------
// File functions

// Adds a listfile into the MPQ archive.
// Note that the function does not remove the 
int WINAPI SFileAddListFile(HANDLE hMpq, const char * szListFile)
{
    TListFileCache * pCache = NULL;
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    HANDLE hListFile = NULL;
    char  szFileName[MAX_PATH + 1];
    DWORD dwSearchScope = SFILE_OPEN_LOCAL_FILE;
    DWORD dwCacheSize = 0;
    DWORD dwFileSize = 0;
    size_t nLength = 0;
    int nError = ERROR_SUCCESS;

    // If the szListFile is NULL, it means we have to open internal listfile
    if(szListFile == NULL)
    {
        // Use SFILE_OPEN_ANY_LOCALE for listfile. This will allow us to load 
        dwSearchScope = SFILE_OPEN_ANY_LOCALE;
        szListFile = LISTFILE_NAME;
    }

    // Open the local/internal listfile
    if(nError == ERROR_SUCCESS)
    {
        if(!SFileOpenFileEx((HANDLE)ha, szListFile, dwSearchScope, &hListFile))
            nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        dwCacheSize = 
        dwFileSize = SFileGetFileSize(hListFile, NULL);

        // Try to allocate memory for the complete file. If it fails,
        // load the part of the file
        pCache = (TListFileCache *)ALLOCMEM(char, (sizeof(TListFileCache) + dwCacheSize));
        if(pCache == NULL)
        {
            dwCacheSize = LISTFILE_CACHE_SIZE;
            pCache = (TListFileCache *)ALLOCMEM(char, sizeof(TListFileCache) + dwCacheSize);
        }

        if(pCache == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    if(nError == ERROR_SUCCESS)
    {
        // Initialize the file cache
        memset(pCache, 0, sizeof(TListFileCache));
        pCache->hFile      = hListFile;
        pCache->dwFileSize = dwFileSize;
        pCache->dwBuffSize = dwCacheSize;
        pCache->dwFilePos  = 0;

        // Fill the cache
        SFileReadFile(hListFile, pCache->Buffer, pCache->dwBuffSize, &pCache->dwBuffSize, NULL);

        // Initialize the pointers
        pCache->pBegin =
        pCache->pPos = &pCache->Buffer[0];
        pCache->pEnd = pCache->pBegin + pCache->dwBuffSize;

        // Load the node list. Add the node for every locale in the archive
        while((nLength = ReadLine(pCache, szFileName, sizeof(szFileName) - 1)) > 0)
            SListFileCreateNodeForAllLocales(ha, szFileName);

        // Also, add three special files to the listfile:
        // (listfile) itself, (attributes) and (signature)
        SListFileCreateNodeForAllLocales(ha, LISTFILE_NAME);
        SListFileCreateNodeForAllLocales(ha, SIGNATURE_NAME);
        SListFileCreateNodeForAllLocales(ha, ATTRIBUTES_NAME);
    }

    // Cleanup & exit
    if(pCache != NULL)
        SListFileFindClose((HANDLE)pCache);
    return nError;
}

//-----------------------------------------------------------------------------
// Passing through the listfile

HANDLE WINAPI SListFileFindFirstFile(HANDLE hMpq, const char * szListFile, const char * szMask, SFILE_FIND_DATA * lpFindFileData)
{
    TListFileCache * pCache = NULL;
    TMPQArchive * ha = (TMPQArchive *)hMpq;
    HANDLE hListFile = NULL;
    DWORD dwSearchScope = SFILE_OPEN_LOCAL_FILE;
    DWORD dwCacheSize = 0;
    DWORD dwFileSize = 0;
    size_t nLength = 0;
    int nError = ERROR_SUCCESS;

    // Initialize the structure with zeros
    memset(lpFindFileData, 0, sizeof(SFILE_FIND_DATA));

    // If the szListFile is NULL, it means we have to open internal listfile
    if(szListFile == NULL)
    {
        szListFile = LISTFILE_NAME;
        dwSearchScope = SFILE_OPEN_FROM_MPQ;
    }

    // Open the local/internal listfile
    if(nError == ERROR_SUCCESS)
    {
        if(!SFileOpenFileEx((HANDLE)ha, szListFile, dwSearchScope, &hListFile))
            nError = GetLastError();
    }

    if(nError == ERROR_SUCCESS)
    {
        dwCacheSize = 
        dwFileSize = SFileGetFileSize(hListFile, NULL);

        // Try to allocate memory for the complete file. If it fails,
        // load the part of the file
        pCache = (TListFileCache *)ALLOCMEM(char, sizeof(TListFileCache) + dwCacheSize);
        if(pCache == NULL)
        {
            dwCacheSize = LISTFILE_CACHE_SIZE;
            pCache = (TListFileCache *)ALLOCMEM(char, sizeof(TListFileCache) + dwCacheSize);
        }

        if(pCache == NULL)
            nError = ERROR_NOT_ENOUGH_MEMORY;
    }

    if(nError == ERROR_SUCCESS)
    {
        // Initialize the file cache
        memset(pCache, 0, sizeof(TListFileCache));
        pCache->hFile      = hListFile;
        pCache->dwFileSize = dwFileSize;
        pCache->dwBuffSize = dwCacheSize;
        pCache->dwFilePos  = 0;
        if(szMask != NULL)
        {
            pCache->szMask = ALLOCMEM(char, strlen(szMask) + 1);
            strcpy(pCache->szMask, szMask);
        }

        // Fill the cache
        SFileReadFile(hListFile, pCache->Buffer, pCache->dwBuffSize, &pCache->dwBuffSize, NULL);

        // Initialize the pointers
        pCache->pBegin =
        pCache->pPos = &pCache->Buffer[0];
        pCache->pEnd = pCache->pBegin + pCache->dwBuffSize;

        for(;;)
        {
            // Read the (next) line
            nLength = ReadLine(pCache, lpFindFileData->cFileName, sizeof(lpFindFileData->cFileName));
            if(nLength == 0)
            {
                nError = ERROR_NO_MORE_FILES;
                break;
            }

            // If some mask entered, check it
            if(CheckWildCard(lpFindFileData->cFileName, pCache->szMask))
                break;                
        }
    }

    // Cleanup & exit
    if(nError != ERROR_SUCCESS)
    {
        memset(lpFindFileData, 0, sizeof(SFILE_FIND_DATA));
        SListFileFindClose((HANDLE)pCache);
        pCache = NULL;

        SetLastError(nError);
    }
    return (HANDLE)pCache;
}

BOOL WINAPI SListFileFindNextFile(HANDLE hFind, SFILE_FIND_DATA * lpFindFileData)
{
    TListFileCache * pCache = (TListFileCache *)hFind;
    size_t nLength;
    BOOL bResult = FALSE;
    int nError = ERROR_SUCCESS;

    for(;;)
    {
        // Read the (next) line
        nLength = ReadLine(pCache, lpFindFileData->cFileName, sizeof(lpFindFileData->cFileName));
        if(nLength == 0)
        {
            nError = ERROR_NO_MORE_FILES;
            break;
        }

        // If some mask entered, check it
        if(CheckWildCard(lpFindFileData->cFileName, pCache->szMask))
        {
            bResult = TRUE;
            break;
        }
    }

    if(nError != ERROR_SUCCESS)
        SetLastError(nError);
    return bResult;
}

BOOL WINAPI SListFileFindClose(HANDLE hFind)
{
    TListFileCache * pCache = (TListFileCache *)hFind;

    if(pCache != NULL)
    {
        if(pCache->hFile != NULL)
            SFileCloseFile(pCache->hFile);
        if(pCache->szMask != NULL)
            FREEMEM(pCache->szMask);

        FREEMEM(pCache);
        return TRUE;
    }

    return FALSE;
}

