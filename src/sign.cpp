#include "sign.h"
#include "asn.h"
#include "crypto.h"
#include "ziparchive.h"

#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

const static int SHA_LEN = 20;
uint32_t crc32_fast(const void* data, size_t length,
                    uint32_t previousCrc32 = 0);

using namespace std;
using namespace asn1;

void sign(ZipArchive& zipArchive, KeyStore& keyStore, const string& digestFile)
{
    SHA_CTX context;
    string head = "Manifest-Version: 1.0\015\012Created-By: 1.0 "
                  "(Fastzip)\015\012\015\012";

    string manifestMF = head + digestFile;
    uint8_t manifestSha[SHA_LEN];
    SHA1_Init(&context);
    SHA1_Update(&context, manifestMF.c_str(), manifestMF.length());
    SHA1_Final(manifestSha, &context);

    uint32_t checksum = crc32_fast(manifestMF.c_str(), manifestMF.length());
    zipArchive.addFile("META-INF/MANIFEST.MF", true, manifestMF.length(),
                       manifestMF.length(), 0, checksum);
    zipArchive.write((uint8_t*)manifestMF.c_str(), manifestMF.length());

    unsigned char sha[SHA_LEN];

    vector<char> digestCopy(digestFile.length() + 1);
    strcpy(&digestCopy[0], digestFile.c_str());

    char* digestPtr = &digestCopy[0];
    while (true) {
        char* ptr = digestPtr;

        while (*ptr) {
            if (ptr[0] == 0x0d && ptr[1] == 0xa && ptr[2] == 0x0d &&
                ptr[3] == 0x0a)
                break;
            ptr++;
        }

        if (!*ptr)
            break;

        ptr += 4;

        SHA1_Init(&context);
        SHA1_Update(&context, digestPtr, ptr - digestPtr);
        SHA1_Final(sha, &context);
        memcpy(ptr - 32, base64_encode(sha, SHA_LEN).c_str(), 28);

        digestPtr = ptr;
    }
    const string sigHead = "Signature-Version: 1.0\015\012Created-By: 1.0 "
                           "(Fastzip)\015\012SHA1-Digest-Manifest: ";

    string certSF = sigHead + base64_encode(manifestSha, SHA_LEN) +
                    "\015\012\015\012" + string(&digestCopy[0]);

    checksum = crc32_fast(certSF.c_str(), certSF.length());
    zipArchive.addFile("META-INF/CERT.SF", true, certSF.length(),
                       certSF.length(), 0, checksum);
    zipArchive.write((uint8_t*)certSF.c_str(), certSF.length());

    unsigned char digest[SHA_LEN];
    memset(digest, 0, SHA_LEN);

    SHA1_Init(&context);
    SHA1_Update(&context, certSF.c_str(), certSF.size());
    SHA1_Final(digest, &context);

    vector<uint8_t> key;
    try {
        key = keyStore.getKey();
    } catch (key_exception& ke) {
        throw sign_exception(ke);
    }

    string pemKey = toPem(key);
    BIO* bio = BIO_new_mem_buf((void*)pemKey.c_str(), -1);
    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
    if (rsa == nullptr)
        throw sign_exception("Could not read valid RSA key");

    vector<uint8_t> certificate = keyStore.getCert();

    // Extract the meta data
    DER certData = readDER(certificate);

    vector<uint8_t> certMetaData;
    uint32_t timeStamp = 0;
    for (auto& cd : certData[0]) {
        if (cd.tag == 0x02 && timeStamp == 0) {
            timeStamp = cd.value;
        }
        if (cd.tag == 0x30 && cd[0].tag == 0x31) {
            certMetaData = cd.data;
            break;
        }
    }

    if (certMetaData.size() == 0)
        throw sign_exception("Could not extract certificate from keystore");

    vector<uint8_t> sign(1024);
    unsigned int signLen;

    RSA_sign(NID_sha1, digest, SHA_LEN, &sign[0], &signLen, rsa);

    RSA_free(rsa);

    BIO_free_all(bio);

    sign.resize(signLen);
    // clang-format off
    auto data =
        mkSEQ(0x30,
            // PKCS7 SIGNED
            mkBIN(0x6, { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02 }),
            mkSEQ(0xA0,
                mkSEQ(0x30,
                    mkINT(1),
                    mkSEQ(0x31,
                        mkSEQ(0x30,
                            // SHA1
                            mkBIN(0x6, { 0x2b, 0x0e, 0x03, 0x02, 0x1a }),
                            mkNIL()
                            )
                        ),
                    mkSEQ(0x30,
                        // PKC7
                        mkBIN(0x6, { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x01 })
                        ),
                    mkSEQ(0xA0, certificate),
                    mkSEQ(0x31,
                        mkSEQ(0x30,
                            mkINT(1),
                            mkSEQ(0x30,
                                mkSEQ(0x30, certMetaData),
                                mkINT(timeStamp)
                                ),
                            mkSEQ(0x30,
                                // SHA1
                                mkBIN(0x6, { 0x2b, 0xe, 0x3, 0x2, 0x1a}),
                                mkNIL()
                                ),
                            mkSEQ(0x30,
                                // RSA ENCRYPTION,
                                mkBIN(0x6, {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 }),
                                mkNIL()
                                ),
                            mkBIN(0x04, sign)
                            )
                        )
                    )
                )
            );
	// clang-format off
    checksum = crc32_fast(&data[0], data.size());
    zipArchive.addFile("META-INF/CERT.RSA", true, data.size(), data.size(), 0, checksum);
    zipArchive.write(&data[0], data.size());
}
