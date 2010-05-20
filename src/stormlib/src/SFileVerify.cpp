/*****************************************************************************/
/* SFileVerify.cpp                        Copyright (c) Ladislav Zezula 2010 */
/*---------------------------------------------------------------------------*/
/* MPQ files and MPQ archives verification.                                  */
/*                                                                           */
/* The MPQ signature verification has been written by Jean-Francois Roy      */
/* <bahamut@macstorm.org> and Justin Olbrantz (Quantam).                     */
/* The MPQ public keys have been created by MPQKit, using OpenSSL library.   */
/*                                                                           */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 04.05.10  1.00  Lad  The first version of SFileVerify.cpp                 */
/*****************************************************************************/

#define __STORMLIB_SELF__
#define __INCLUDE_COMPRESSION__
#define __INCLUDE_CRYPTOGRAPHY__
#include "StormLib.h"
#include "SCommon.h"

//-----------------------------------------------------------------------------
// Local defines

#define SIGNATURE_TYPE_NONE             0
#define SIGNATURE_TYPE_WEAK             1
#define SIGNATURE_TYPE_STRONG           2

#define MPQ_WEAK_SIGNATURE_SIZE        64
#define MPQ_STRONG_SIGNATURE_SIZE     256 

#define MPQ_DIGEST_UNIT_SIZE      0x10000

typedef struct _MPQ_SIGNATURE_INFO
{
    LARGE_INTEGER BeginMpqData;             // File offset where the hashing starts
    LARGE_INTEGER BeginExclude;             // Begin of the excluded area (used for (signature) file)
    LARGE_INTEGER EndExclude;               // End of the excluded area (used for (signature) file)
    LARGE_INTEGER EndMpqData;               // File offset where the hashing ends
    LARGE_INTEGER EndOfFile;                // Size of the entire file
    BYTE  Signature[MPQ_STRONG_SIGNATURE_SIZE + 0x10];
    DWORD cbSignatureSize;                  // Length of the signature
    int nSignatureType;                     // See SIGNATURE_TYPE_XXX

} MPQ_SIGNATURE_INFO, *PMPQ_SIGNATURE_INFO;

//-----------------------------------------------------------------------------
// Known Blizzard public keys
// Created by Jean-Francois Roy using OpenSSL

static const char * szBlizzardWeakPublicKey =
    "-----BEGIN PUBLIC KEY-----"
    "MFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAJJidwS/uILMBSO5DLGsBFknIXWWjQJe"
    "2kfdfEk3G/j66w4KkhZ1V61Rt4zLaMVCYpDun7FLwRjkMDSepO1q2DcCAwEAAQ=="
    "-----END PUBLIC KEY-----";

static const char * szBlizzardStrongPublicKey =
    "-----BEGIN PUBLIC KEY-----"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsQZ+ziT2h8h+J/iMQpgd"
    "tH1HaJzOBE3agjU4yMPcrixaPOZoA4t8bwfey7qczfWywocYo3pleytFF+IuD4HD"
    "Fl9OXN1SFyupSgMx1EGZlgbFAomnbq9MQJyMqQtMhRAjFgg4TndS7YNb+JMSAEKp"
    "kXNqY28n/EVBHD5TsMuVCL579gIenbr61dI92DDEdy790IzIG0VKWLh/KOTcTJfm"
    "Ds/7HQTkGouVW+WUsfekuqNQo7ND9DBnhLjLjptxeFE2AZqYcA1ao3S9LN3GL1tW"
    "lVXFIX9c7fWqaVTQlZ2oNsI/ARVApOK3grNgqvwH6YoVYVXjNJEo5sQJsPsdV/hk"
    "dwIDAQAB"
    "-----END PUBLIC KEY-----";

static const char * szWarcraft3MapPublicKey =
    "-----BEGIN PUBLIC KEY-----"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA1BwklUUQ3UvjizOBRoF5"
    "yyOVc7KD+oGOQH5i6eUk1yfs0luCC70kNucNrfqhmviywVtahRse1JtXCPrx2bd3"
    "iN8Dx91fbkxjYIOGTsjYoHKTp0BbaFkJih776fcHgnFSb+7mJcDuJVvJOXxEH6w0"
    "1vo6VtujCqj1arqbyoal+xtAaczF3us5cOEp45sR1zAWTn1+7omN7VWV4QqJPaDS"
    "gBSESc0l1grO0i1VUSumayk7yBKIkb+LBvcG6WnYZHCi7VdLmaxER5m8oZfER66b"
    "heHoiSQIZf9PAY6Guw2DT5BTc54j/AaLQAKf2qcRSgQLVo5kQaddF3rCpsXoB/74"
    "6QIDAQAB"
    "-----END PUBLIC KEY-----";

static const char * szWowPatchPublicKey =
    "-----BEGIN PUBLIC KEY-----"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAwOsMV0LagAWPEtEQM6b9"
    "6FHFkUyGbbyda2/Dfc9dyl21E9QvX+Yw7qKRMAKPzA2TlQQLZKvXpnKXF/YIK5xa"
    "5uwg9CEHCEAYolLG4xn0FUOE0E/0PuuytI0p0ICe6rk00PifZzTr8na2wI/l/GnQ"
    "bvnIVF1ck6cslATpQJ5JJVMXzoFlUABS19WESw4MXuJAS3AbMhxNWdEhVv7eO51c"
    "yGjRLy9QjogZODZTY0fSEksgBqQxNCoYVJYI/sF5K2flDsGqrIp0OdJ6teJlzg1Y"
    "UjYnb6bKjlidXoHEXI2TgA/mD6O3XFIt08I9s3crOCTgICq7cgX35qrZiIVWZdRv"
    "TwIDAQAB"
    "-----END PUBLIC KEY-----";

static const char * szWowSurveyPublicKey =
    "-----BEGIN PUBLIC KEY-----"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnIt1DR6nRyyKsy2qahHe"
    "MKLtacatn/KxieHcwH87wLBxKy+jZ0gycTmJ7SaTdBAEMDs/V5IPIXEtoqYnid2c"
    "63TmfGDU92oc3Ph1PWUZ2PWxBhT06HYxRdbrgHw9/I29pNPi/607x+lzPORITOgU"
    "BR6MR8au8HsQP4bn4vkJNgnSgojh48/XQOB/cAln7As1neP61NmVimoLR4Bwi3zt"
    "zfgrZaUpyeNCUrOYJmH09YIjbBySTtXOUidoPHjFrMsCWpr6xs8xbETbs7MJFL6a"
    "vcUfTT67qfIZ9RsuKfnXJTIrV0kwDSjjuNXiPTmWAehSsiHIsrUXX5RNcwsSjClr"
    "nQIDAQAB"
    "-----END PUBLIC KEY-----";

static const char * szStarcraft2MapPublicKey =
    "-----BEGIN PUBLIC KEY-----"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAmk4GT8zb+ICC25a17KZB"
    "q/ygKGJ2VSO6IT5PGHJlm1KfnHBA4B6SH3xMlJ4c6eG2k7QevZv+FOhjsAHubyWq"
    "2VKqWbrIFKv2ILc2RfMn8J9EDVRxvcxh6slRrVL69D0w1tfVGjMiKq2Fym5yGoRT"
    "E7CRgDqbAbXP9LBsCNWHiJLwfxMGzHbk8pIl9oia5pvM7ofZamSHchxlpy6xa4GJ"
    "7xKN01YCNvklTL1D7uol3wkwcHc7vrF8QwuJizuA5bSg4poEGtH62BZOYi+UL/z0"
    "31YK+k9CbQyM0X0pJoJoYz1TK+Y5J7vBnXCZtfcTYQ/ZzN6UcxTa57dJaiOlCh9z"
    "nQIDAQAB"
    "-----END PUBLIC KEY-----";

//-----------------------------------------------------------------------------
// Local functions

static void memrev(unsigned char *buf, size_t count)
{
    unsigned char *r;

    for (r = buf + count - 1; buf < r; buf++, r--)
    {
        *buf ^= *r;
        *r   ^= *buf;
        *buf ^= *r;
    }
}

static bool is_valid_md5(void * pvMd5)
{
    unsigned char * pbMd5 = (unsigned char *)pvMd5;
    unsigned char ByteSum = 0;
    int i;

    for(i = 0; i < 0x10; i++)
        ByteSum |= pbMd5[i];

    return (ByteSum != 0) ? true : false;
}

static bool decode_base64_key(const char * szKeyBase64, rsa_key * key)
{
    unsigned char decoded_key[0x200];
    const char * szBase64Begin;
    const char * szBase64End;
    unsigned long decoded_length = sizeof(decoded_key);
    unsigned long length;

    // Find out the begin of the BASE64 data
    szBase64Begin = szKeyBase64 + strlen("-----BEGIN PUBLIC KEY-----");
    szBase64End   = szBase64Begin + strlen(szBase64Begin) - strlen("-----END PUBLIC KEY-----");
    if(szBase64End[0] != '-')
        return false;

    // decode the base64 string
    length = (unsigned long)(szBase64End - szBase64Begin);
    if(base64_decode((unsigned char *)szBase64Begin, length, decoded_key, &decoded_length) != CRYPT_OK)
        return false;

    // Create RSA key
    if(rsa_import(decoded_key, decoded_length, key) != CRYPT_OK)
        return false;

    return true;
}

// Calculate begin and end of the MPQ archive
static void CalculateArchiveRange(
    TMPQArchive * ha,
    PMPQ_SIGNATURE_INFO pSI)
{
    LARGE_INTEGER TempPos;
    LARGE_INTEGER MaxPos;
    DWORD dwBytesRead = 0;
    char szMapHeader[0x200];

    // Get the MPQ begin
    pSI->BeginMpqData.QuadPart = ha->MpqPos.QuadPart;

    // Warcraft III maps are signed from the map header to the end
    SetFilePointer(ha->hFile, 0, NULL, FILE_BEGIN);
    ReadFile(ha->hFile, szMapHeader, sizeof(szMapHeader), &dwBytesRead, NULL);
    if(dwBytesRead == sizeof(szMapHeader))
    {
        // Is it a map header ?
        if(szMapHeader[0] == 'H' && szMapHeader[1] == 'M' && szMapHeader[2] == '3' && szMapHeader[3] == 'W')
        {
            // We will have to hash since the map header
            pSI->BeginMpqData.QuadPart = 0;
        }
    }

    // Get the MPQ data end. The end is calculated as the biggest
    // value of (end of the last file), (end of block table),
    // (end of ext block table), (end of hash table)
    FindFreeMpqSpace(ha, &MaxPos);

    // Check if hash table is beyond
    TempPos.QuadPart = ha->HashTablePos.QuadPart + (ha->pHeader->dwHashTableSize * sizeof(TMPQHash));
    if(TempPos.QuadPart > MaxPos.QuadPart)
        MaxPos.QuadPart = TempPos.QuadPart;

    // Check if block table is beyond
    TempPos.QuadPart = ha->BlockTablePos.QuadPart + (ha->pHeader->dwBlockTableSize * sizeof(TMPQBlock));
    if(TempPos.QuadPart > MaxPos.QuadPart)
        MaxPos.QuadPart = TempPos.QuadPart;

    // Check if ext block table is beyond
    if(ha->ExtBlockTablePos.QuadPart != 0)
    {
        TempPos.QuadPart = ha->ExtBlockTablePos.QuadPart + (ha->pHeader->dwBlockTableSize * sizeof(TMPQBlockEx));
        if(TempPos.QuadPart > MaxPos.QuadPart)
            MaxPos.QuadPart = TempPos.QuadPart;
    }

    // Give the end
    pSI->EndMpqData.QuadPart = TempPos.QuadPart;

    // Get the size of the entire file
    pSI->EndOfFile.LowPart = GetFileSize(ha->hFile, (LPDWORD)&pSI->EndOfFile.HighPart);
}

static bool QueryMpqSignatureInfo(
    TMPQArchive * ha,
    PMPQ_SIGNATURE_INFO pSI)
{
    LARGE_INTEGER ExtraBytes;
    TMPQFile * hf;
    HANDLE hFile;
    DWORD dwFileSize;

    // Calculate the range of the MPQ
    CalculateArchiveRange(ha, pSI);

    // If there is "(signature)" file in the MPQ, it has a weak signature
    if(SFileOpenFileEx((HANDLE)ha, SIGNATURE_NAME, SFILE_OPEN_FROM_MPQ, &hFile))
    {
        // Get the content of the signature
        SFileReadFile(hFile, pSI->Signature, sizeof(pSI->Signature), &pSI->cbSignatureSize);

        // Verify the size of the signature
        hf = (TMPQFile *)hFile;

        // We have to exclude the signature file from the digest
        pSI->BeginExclude.QuadPart = ha->MpqPos.QuadPart + hf->pBlock->dwFilePos;
        pSI->EndExclude.QuadPart = pSI->BeginExclude.QuadPart + hf->pBlock->dwCSize;
        dwFileSize = hf->pBlock->dwFSize;

        // Close the file
        SFileCloseFile(hFile);
        pSI->nSignatureType = SIGNATURE_TYPE_WEAK;
        return (dwFileSize == (MPQ_WEAK_SIGNATURE_SIZE + 8)) ? true : false;
    }

    // If there is extra bytes beyond the end of the archive,
    // it's the strong signature
    ExtraBytes.QuadPart = pSI->EndOfFile.QuadPart - pSI->EndMpqData.QuadPart;
    if(ExtraBytes.HighPart == 0 && ExtraBytes.LowPart >= (MPQ_STRONG_SIGNATURE_SIZE + 4))
    {
        // Read the strong signature
        SetFilePointer(ha->hFile, pSI->EndMpqData.LowPart, &pSI->EndMpqData.HighPart, FILE_BEGIN);
        ReadFile(ha->hFile, pSI->Signature, (MPQ_STRONG_SIGNATURE_SIZE + 4), &pSI->cbSignatureSize, NULL);
        if(pSI->cbSignatureSize != (MPQ_STRONG_SIGNATURE_SIZE + 4))
            return false;

        // Check the signature header "NGIS"
        if(pSI->Signature[0] != 'N' || pSI->Signature[1] != 'G' || pSI->Signature[2] != 'I' || pSI->Signature[3] != 'S')
            return false;

        pSI->nSignatureType = SIGNATURE_TYPE_STRONG;
        return true;
    }

    // Succeeded, but no known signature found
    return true;
}

static bool CalculateMpqHashMd5(
    TMPQArchive * ha,
    PMPQ_SIGNATURE_INFO pSI,
    BYTE * pMd5Digest)
{
    LARGE_INTEGER BeginBuffer;
    LARGE_INTEGER EndBuffer;
    hash_state md5_state;
    LPBYTE pbDigestBuffer = NULL;
    DWORD dwBytesRead = 0;

    // Allocate buffer for creating the MPQ digest.
    pbDigestBuffer = ALLOCMEM(BYTE, MPQ_DIGEST_UNIT_SIZE);
    if(pbDigestBuffer == NULL)
        return false;

    // Initialize the MD5 hash state
    md5_init(&md5_state);

    // Set the MPQ file pointer to the begin of the hashed area
    SetFilePointer(ha->hFile, pSI->BeginMpqData.LowPart, &pSI->BeginMpqData.HighPart, FILE_BEGIN);
    BeginBuffer = pSI->BeginMpqData;

    // Create the digest
    for(;;)
    {
        LARGE_INTEGER BytesRemaining;
        LPBYTE pbSigBegin = NULL;
        LPBYTE pbSigEnd = NULL;
        DWORD dwToRead = MPQ_DIGEST_UNIT_SIZE;

        // Check the number of bytes remaining
        BytesRemaining.QuadPart = pSI->EndMpqData.QuadPart - BeginBuffer.QuadPart;
        if(BytesRemaining.HighPart == 0 && BytesRemaining.LowPart < MPQ_DIGEST_UNIT_SIZE)
            dwToRead = BytesRemaining.LowPart;

        // Read the next chunk 
        ReadFile(ha->hFile, pbDigestBuffer, dwToRead, &dwBytesRead, NULL);
        if(dwBytesRead == 0)
            break;
        EndBuffer.QuadPart = BeginBuffer.QuadPart + dwBytesRead;

        // Check if the signature is within the loaded digest
        if(BeginBuffer.QuadPart <= pSI->BeginExclude.QuadPart && pSI->BeginExclude.QuadPart < EndBuffer.QuadPart)
            pbSigBegin = pbDigestBuffer + (size_t)(pSI->BeginExclude.QuadPart - BeginBuffer.QuadPart);
        if(BeginBuffer.QuadPart <= pSI->EndExclude.QuadPart && pSI->EndExclude.QuadPart < EndBuffer.QuadPart)
            pbSigEnd = pbDigestBuffer + (size_t)(pSI->EndExclude.QuadPart - BeginBuffer.QuadPart);

        // Zero the part that belongs to the signature
        if(pbSigBegin != NULL || pbSigEnd != NULL)
        {
            if(pbSigBegin == NULL)
                pbSigBegin = pbDigestBuffer;
            if(pbSigEnd == NULL)
                pbSigEnd = pbDigestBuffer + dwBytesRead;

            memset(pbSigBegin, 0, (pbSigEnd - pbSigBegin));
        }

        // Pass the buffer to the hashing function
        md5_process(&md5_state, pbDigestBuffer, dwBytesRead);

        // Move pointers
        BeginBuffer.QuadPart += dwBytesRead;
    }

    // Finalize the MD5 hash
    md5_done(&md5_state, pMd5Digest);
    FREEMEM(pbDigestBuffer);
    return true;
}

static void AddTailToSha1(
    hash_state * psha1_state,
    const char * szTail)
{
    unsigned char szUpperCase[0x200];
    unsigned long nLength = 0;

    // Convert the tail to uppercase
    // Note that we don't need to terminate the string with zero
    while(*szTail != 0)
    {
        szUpperCase[nLength++] = (unsigned char)toupper(*szTail++);
    }

    // Append the tail to the SHA1
    sha1_process(psha1_state, szUpperCase, nLength);
}


static bool CalculateMpqHashSha1(
    TMPQArchive * ha,
    PMPQ_SIGNATURE_INFO pSI,
    unsigned char * sha1_tail0,
    unsigned char * sha1_tail1,
    unsigned char * sha1_tail2)
{
    LARGE_INTEGER BeginBuffer;
    hash_state sha1_state_temp;
    hash_state sha1_state;
    LPBYTE pbDigestBuffer = NULL;
    DWORD dwBytesRead = 0;

    // Allocate buffer for creating the MPQ digest.
    pbDigestBuffer = ALLOCMEM(BYTE, MPQ_DIGEST_UNIT_SIZE);
    if(pbDigestBuffer == NULL)
        return false;

    // Initialize SHA1 state structure
    sha1_init(&sha1_state);

    // Set the MPQ file pointer to the begin of the MPQ
    SetFilePointer(ha->hFile, pSI->BeginMpqData.LowPart, &pSI->BeginMpqData.HighPart, FILE_BEGIN);
    BeginBuffer = pSI->BeginMpqData;

    // Create the digest
    for(;;)
    {
        LARGE_INTEGER BytesRemaining;
        DWORD dwToRead = MPQ_DIGEST_UNIT_SIZE;

        // Check the number of bytes remaining
        BytesRemaining.QuadPart = pSI->EndMpqData.QuadPart - BeginBuffer.QuadPart;
        if(BytesRemaining.HighPart == 0 && BytesRemaining.LowPart < MPQ_DIGEST_UNIT_SIZE)
            dwToRead = BytesRemaining.LowPart;

        // Read the next chunk 
        ReadFile(ha->hFile, pbDigestBuffer, dwToRead, &dwBytesRead, NULL);
        if(dwBytesRead == 0)
            break;

        // Pass the buffer to the hashing function
        sha1_process(&sha1_state, pbDigestBuffer, dwBytesRead);

        // Move pointers
        BeginBuffer.QuadPart += dwBytesRead;
    }

    // Add all three known tails and generate three hashes
    memcpy(&sha1_state_temp, &sha1_state, sizeof(hash_state));
    sha1_done(&sha1_state_temp, sha1_tail0);

    memcpy(&sha1_state_temp, &sha1_state, sizeof(hash_state));
    AddTailToSha1(&sha1_state_temp, GetPlainLocalFileName(ha->szFileName));
    sha1_done(&sha1_state_temp, sha1_tail1);

    memcpy(&sha1_state_temp, &sha1_state, sizeof(hash_state));
    AddTailToSha1(&sha1_state_temp, "ARCHIVE");
    sha1_done(&sha1_state_temp, sha1_tail2);

    // Finalize the MD5 hash
    FREEMEM(pbDigestBuffer);
    return true;
}

static DWORD VerifyWeakSignature(
    TMPQArchive * ha,
    PMPQ_SIGNATURE_INFO pSI)
{
    BYTE RevSignature[MPQ_WEAK_SIGNATURE_SIZE];
    BYTE Md5Digest[MD5_DIGEST_SIZE];
    rsa_key key;
    int hash_idx = find_hash("md5");
    int result = 0;

    // Calculate hash of the entire archive, skipping the (signature) file
    if(!CalculateMpqHashMd5(ha, pSI, Md5Digest))
        return ERROR_VERIFY_FAILED;

    // Import the Blizzard key in OpenSSL format
    if(!decode_base64_key(szBlizzardWeakPublicKey, &key))
        return ERROR_VERIFY_FAILED;

    // Verify the signature
    memcpy(RevSignature, &pSI->Signature[8], MPQ_WEAK_SIGNATURE_SIZE);
    memrev(RevSignature, MPQ_WEAK_SIGNATURE_SIZE);
    rsa_verify_hash_ex(RevSignature, MPQ_WEAK_SIGNATURE_SIZE, Md5Digest, sizeof(Md5Digest), LTC_LTC_PKCS_1_V1_5, hash_idx, 0, &result, &key);
    rsa_free(&key);

    // Return the result
    return result ? ERROR_WEAK_SIGNATURE_OK : ERROR_WEAK_SIGNATURE_ERROR;
}

static DWORD VerifyStrongSignatureWithKey(
    unsigned char * reversed_signature,
    unsigned char * padded_digest,
    const char * szPublicKey)
{
    rsa_key key;
    int result = 0;

    // Import the Blizzard key in OpenSSL format
    if(!decode_base64_key(szPublicKey, &key))
    {
        assert(FALSE);
        return ERROR_VERIFY_FAILED;
    }

    // Verify the signature
    if(rsa_verify_simple(reversed_signature, MPQ_STRONG_SIGNATURE_SIZE, padded_digest, MPQ_STRONG_SIGNATURE_SIZE, &result, &key) != CRYPT_OK)
        return ERROR_VERIFY_FAILED;
    
    // Free the key and return result
    rsa_free(&key);
    return result ? ERROR_STRONG_SIGNATURE_OK : ERROR_STRONG_SIGNATURE_ERROR;
}

static DWORD VerifyStrongSignature(
    TMPQArchive * ha,
    PMPQ_SIGNATURE_INFO pSI)
{
    unsigned char reversed_signature[MPQ_STRONG_SIGNATURE_SIZE];
    unsigned char Sha1Digest_tail0[SHA1_DIGEST_SIZE];
    unsigned char Sha1Digest_tail1[SHA1_DIGEST_SIZE];
    unsigned char Sha1Digest_tail2[SHA1_DIGEST_SIZE];
    unsigned char padded_digest[MPQ_STRONG_SIGNATURE_SIZE];
    DWORD dwResult;
    size_t digest_offset;

    // Calculate SHA1 hash of the archive
    if(!CalculateMpqHashSha1(ha, pSI, Sha1Digest_tail0, Sha1Digest_tail1, Sha1Digest_tail2))
        return ERROR_VERIFY_FAILED;

    // Prepare the signature for decryption
    memcpy(reversed_signature, &pSI->Signature[4], MPQ_STRONG_SIGNATURE_SIZE);
    memrev(reversed_signature, MPQ_STRONG_SIGNATURE_SIZE);

    // Prepare the padded digest for comparison
    digest_offset = sizeof(padded_digest) - SHA1_DIGEST_SIZE;
    memset(padded_digest, 0xbb, digest_offset);
    padded_digest[0] = 0x0b;

    // Try Blizzard Strong public key with no SHA1 tail
    memcpy(padded_digest + digest_offset, Sha1Digest_tail0, SHA1_DIGEST_SIZE);
    memrev(padded_digest + digest_offset, SHA1_DIGEST_SIZE);
    dwResult = VerifyStrongSignatureWithKey(reversed_signature, padded_digest, szBlizzardStrongPublicKey);
    if(dwResult == ERROR_STRONG_SIGNATURE_OK)
        return dwResult;

    // Try War 3 map public key with plain file name as SHA1 tail
    memcpy(padded_digest + digest_offset, Sha1Digest_tail1, SHA1_DIGEST_SIZE);
    memrev(padded_digest + digest_offset, SHA1_DIGEST_SIZE);
    dwResult = VerifyStrongSignatureWithKey(reversed_signature, padded_digest, szWarcraft3MapPublicKey);
    if(dwResult == ERROR_STRONG_SIGNATURE_OK)
        return dwResult;

    // Try WoW-TBC public key with "ARCHIVE" as SHA1 tail
    memcpy(padded_digest + digest_offset, Sha1Digest_tail2, SHA1_DIGEST_SIZE);
    memrev(padded_digest + digest_offset, SHA1_DIGEST_SIZE);
    dwResult = VerifyStrongSignatureWithKey(reversed_signature, padded_digest, szWowPatchPublicKey);
    if(dwResult == ERROR_STRONG_SIGNATURE_OK)
        return dwResult;

    // Try Survey public key with no SHA1 tail
    memcpy(padded_digest + digest_offset, Sha1Digest_tail0, SHA1_DIGEST_SIZE);
    memrev(padded_digest + digest_offset, SHA1_DIGEST_SIZE);
    dwResult = VerifyStrongSignatureWithKey(reversed_signature, padded_digest, szWowSurveyPublicKey);
    if(dwResult == ERROR_STRONG_SIGNATURE_OK)
        return dwResult;

    // Try Starcraft II public key with no SHA1 tail
    memcpy(padded_digest + digest_offset, Sha1Digest_tail0, SHA1_DIGEST_SIZE);
    memrev(padded_digest + digest_offset, SHA1_DIGEST_SIZE);
    dwResult = VerifyStrongSignatureWithKey(reversed_signature, padded_digest, szStarcraft2MapPublicKey);
    if(dwResult == ERROR_STRONG_SIGNATURE_OK)
        return dwResult;

    return ERROR_STRONG_SIGNATURE_ERROR;
}

//-----------------------------------------------------------------------------
// Public (exported) functions

DWORD WINAPI SFileVerifyFile(HANDLE hMpq, const char * szFileName, DWORD dwFlags)
{
    hash_state md5_state;
    TMPQFile * hf;
    TMPQMD5 Md5;
    BYTE Buffer[0x1000];
    HANDLE hFile = NULL;
    DWORD dwVerifyResult = 0;
    DWORD dwTotalBytes = 0;
    DWORD dwBytesRead;
    DWORD dwCrc32;

    // Attempt to open the file
    if(SFileOpenFileEx(hMpq, szFileName, SFILE_OPEN_FROM_MPQ, &hFile))
    {
        // Get the file size
        hf = (TMPQFile *)hFile;
        SFileGetFileInfo(hFile, SFILE_INFO_FILE_SIZE, &dwTotalBytes, sizeof(DWORD));

        // Initialize the CRC32 and MD5 contexts
        md5_init(&md5_state);
        dwCrc32 = crc32(0, Z_NULL, 0);

        // Also turn on sector checksum verification
        hf->bCheckSectorCRCs = TRUE;

        // Go through entire file and update both CRC32 and MD5
        for(;;)
        {
            // Read data from file
            SFileReadFile(hFile, Buffer, sizeof(Buffer), &dwBytesRead, NULL);
            if(dwBytesRead == 0)
            {
                if(GetLastError() == ERROR_CHECKSUM_ERROR)
                    dwVerifyResult |= VERIFY_ERROR_SECTOR_CHECKSUM;
                break;
            }

            // Update CRC32 value
            if(dwFlags & MPQ_ATTRIBUTE_CRC32)
                dwCrc32 = crc32(dwCrc32, Buffer, dwBytesRead);
            
            // Update MD5 value
            if(dwFlags & MPQ_ATTRIBUTE_MD5)
                md5_process(&md5_state, Buffer, dwBytesRead);

            // Decrement the total size
            dwTotalBytes -= dwBytesRead;
        }

        // Check if total bytes loaded matches
        if(dwTotalBytes != 0)
            dwVerifyResult |= VERIFY_ERROR_READ_FILE;

        // Check if the CRC32 matches
        if((dwFlags & MPQ_ATTRIBUTE_CRC32) && hf->pCrc32 != NULL)
        {
            // Some files may have their CRC zeroed
            if(hf->pCrc32[0] != 0 && dwCrc32 != hf->pCrc32[0])
                dwVerifyResult |= VERIFY_ERROR_FILE_CHECKSUM;
        }

        // Check if MD5 matches
        if((dwFlags & MPQ_ATTRIBUTE_MD5) && hf->pMd5 != NULL)
        {
            md5_done(&md5_state, Md5.Value);

            // Some files have the MD5 zeroed. Don't check MD5 in that case
            if(is_valid_md5(hf->pMd5->Value))
            {
                if(memcmp(Md5.Value, hf->pMd5->Value, sizeof(TMPQMD5)))
                    dwVerifyResult |= VERIFY_ERROR_FILE_MD5;
            }
        }

        SFileCloseFile(hFile);
    }
    else
    {
        dwVerifyResult |= VERIFY_ERROR_OPEN_FILE;
    }

    return dwVerifyResult;
}

// Verifies the archive against the signature
DWORD WINAPI SFileVerifyArchive(HANDLE hMpq)
{
    MPQ_SIGNATURE_INFO si;
    TMPQArchive * ha = (TMPQArchive *)hMpq;

    // Verify input parameters
    if(!IsValidMpqHandle(ha))
        return ERROR_VERIFY_FAILED;

    // Get the MPQ signature and signature type
    memset(&si, 0, sizeof(MPQ_SIGNATURE_INFO));
    if(!QueryMpqSignatureInfo(ha, &si))
        return ERROR_VERIFY_FAILED;

    // Verify the signature
    switch(si.nSignatureType)
    {
        case SIGNATURE_TYPE_NONE:
            return ERROR_NO_SIGNATURE;

        case SIGNATURE_TYPE_WEAK:
            return VerifyWeakSignature(ha, &si);

        case SIGNATURE_TYPE_STRONG:
            return VerifyStrongSignature(ha, &si);
    }

    return ERROR_VERIFY_FAILED;
}
