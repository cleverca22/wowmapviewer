/*****************************************************************************/
/* SCommon.h                              Copyright (c) Ladislav Zezula 2003 */
/*---------------------------------------------------------------------------*/
/* Common functions for encryption/decryption from Storm.dll. Included by    */
/* SFile*** functions, do not include and do not use this file directly      */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 24.03.03  1.00  Lad  The first version of SFileCommon.h                   */
/* 12.06.04  1.00  Lad  Renamed to SCommon.h                                 */
/*****************************************************************************/

#ifndef __SCOMMON_H__
#define __SCOMMON_H__

//-----------------------------------------------------------------------------
// Make sure that we include the compression library headers,
// when a source needs the functions

#ifdef __INCLUDE_COMPRESSION__

  #include "pklib/pklib.h"          // Include functions from Pkware Data Compression Library

  #include "huffman/huff.h"         // Include functions from Huffmann compression

  #include "adpcm/adpcm.h"          // Include functions from IMA ADPCM compression

  #include "sparse/sparse.h"        // Include functions from SPARSE compression

  #include "lzma/C/LzmaEnc.h"       // Include functions from LZMA compression
  #include "lzma/C/LzmaDec.h"

  #ifndef __SYS_ZLIB
    #include "zlib/zlib.h"          // Include functions from zlib
  #else
    #include <zlib.h>               // If zlib is available on system, use this instead
  #endif

  #ifndef __SYS_BZLIB
    #include "bzip2/bzlib.h"        // Include functions from bzlib
  #else
    #include <bzlib.h>              // If bzlib is available on system, use this instead
  #endif

#endif

//-----------------------------------------------------------------------------
// Make sure that we include the cryptography headers,
// when a source needs the functions

#ifdef __INCLUDE_CRYPTOGRAPHY__
#include "libtomcrypt/src/headers/tomcrypt.h"
#endif

//-----------------------------------------------------------------------------
// StormLib private defines

// Prevent problems with CRT "min" and "max" functions,
// as they are not defined on all platforms
#define STORMLIB_MIN(a, b) ((a < b) ? a : b)
#define STORMLIB_MAX(a, b) ((a > b) ? a : b)

extern LCID lcFileLocale;                   // Preferred file locale

//-----------------------------------------------------------------------------
// Encryption and decryption functions

void InitializeMpqCryptography();

DWORD DecryptFileKey(const char * szFileName);

void  EncryptMpqBlock(VOID * pvFileBlock, DWORD dwLength, DWORD dwFileKey);
void  DecryptMpqBlock(VOID * pvFileBlock, DWORD dwLength, DWORD dwFileKey);

void  EncryptMpqTable(VOID * pvMpqTable, DWORD dwLength, const char * szKey);
void  DecryptMpqTable(VOID * pvMpqTable, DWORD dwLength, const char * szKey);

DWORD DetectFileKeyBySectorSize(DWORD * SectorOffsets, DWORD decrypted);
DWORD DetectFileKeyByContent(VOID * pvFileContent, DWORD dwFileSize);

//-----------------------------------------------------------------------------
// Handle validation functions

BOOL IsValidMpqHandle(TMPQArchive * ha);
BOOL IsValidFileHandle(TMPQFile * hf);

//-----------------------------------------------------------------------------
// Hash table and block table manipulation

TMPQHash * GetFirstHashEntry(TMPQArchive * ha, const char * szFileName);
TMPQHash * GetNextHashEntry(TMPQArchive * ha, TMPQHash * pFirstHash, TMPQHash * pPrevHash);
TMPQHash * GetHashEntryLocale(TMPQArchive * ha, const char * szFileName, LCID lcLocale);
TMPQHash * GetHashEntryExact(TMPQArchive * ha, const char * szFileName, LCID lcLocale);
TMPQHash * GetHashEntryAny(TMPQArchive * ha, const char * szFileName);
TMPQHash * GetHashEntryByIndex(TMPQArchive * ha, DWORD dwFileIndex);

TMPQHash * FindFreeHashEntry(TMPQHash * pHashTable, DWORD dwHashTableSize, const char * szFileName);
TMPQHash * FindFreeHashEntry(TMPQArchive * ha, const char * szFileName);
DWORD FindFreeMpqSpace(TMPQArchive * ha, PLARGE_INTEGER pMpqPos);

//-----------------------------------------------------------------------------
// Common functions - MPQ File

TMPQFile * CreateMpqFile(TMPQArchive * ha, const char * szFileName);
int  AllocateSectorBuffer(TMPQFile * hf);
int  AllocateSectorOffsets(TMPQFile * hf, bool bLoadFromFile);
int  AllocateSectorChecksums(TMPQFile * hf, bool bLoadFromFile);
void CalculateRawSectorOffset(LARGE_INTEGER & RawFilePos, TMPQFile * hf, DWORD dwSectorOffset);
int  WriteSectorOffsets(TMPQFile * hf);
int  WriteSectorChecksums(TMPQFile * hf);
void FreeMPQFile(TMPQFile *& hf);

int  SaveMPQTables(TMPQArchive * ha);
void FreeMPQArchive(TMPQArchive *& ha);

//-----------------------------------------------------------------------------
// Utility functions

BOOL CheckWildCard(const char * szString, const char * szWildCard);
const char * GetPlainLocalFileName(const char * szFileName);
const char * GetPlainMpqFileName(const char * szFileName);

//-----------------------------------------------------------------------------
// Support for adding files to the MPQ

int SFileAddFile_Init(
    TMPQArchive * ha,
    const char * szArchivedName,
    TMPQFileTime * pFT,
    DWORD dwFileSize,
    LCID lcLocale,
    DWORD dwFlags,
    void ** ppvAddHandle
    );

int SFileAddFile_Write(
    void * pvAddHandle,
    void * pvData,
    DWORD dwSize,
    DWORD dwCompression
    );

int SFileAddFile_Finish(
    void * pvAddHandle,
    int nError
    );

//-----------------------------------------------------------------------------
// Attributes support

int  SAttrCreateAttributes(TMPQArchive * ha, DWORD dwFlags);
int  SAttrLoadAttributes(TMPQArchive * ha);
int  SAttrFileSaveToMpq(TMPQArchive * ha);
void FreeMPQAttributes(TMPQAttributes * pAttr);

//-----------------------------------------------------------------------------
// Listfile functions

int  SListFileCreateListFile(TMPQArchive * ha);
int  SListFileCreateNode(TMPQArchive * ha, const char * szFileName, TMPQHash * pAddedHash);
int  SListFileRemoveNode(TMPQArchive * ha, TMPQHash * pHash);
void SListFileFreeListFile(TMPQArchive * ha);

int  SListFileSaveToMpq(TMPQArchive * ha);

#endif // __SCOMMON_H__

