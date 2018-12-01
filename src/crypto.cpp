#include "crypto.h"

#include <assert.h>
#include <openssl/sha.h>

using namespace asn1;

extern std::vector<unsigned char> fastzip_keystore;

static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789+/";

std::string base64_encode(unsigned char const* bytes_to_encode,
                          unsigned int in_len)
{
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) +
                              ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) +
                              ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] =
            ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] =
            ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

std::string toPem(std::vector<uint8_t> key)
{
    std::string rc = "-----BEGIN RSA PRIVATE KEY-----\n";
    int pos = 0;
    int l = key.size();
    while (pos < l) {
        rc += base64_encode(&key[pos], l - pos < 48 ? l - pos : 48);
        rc += "\n";
        pos += 48;
    }
    rc += "-----END RSA PRIVATE KEY-----\n";
    return rc;
}

const int SALT_LEN = 20;
const int DIGEST_LEN = 20;

#define arraycopy8(source, soff, target, toff, len)                            \
    memcpy(&target[toff], &source[soff], len)

std::vector<uint8_t> recoverKey(const std::string& password,
                                const std::vector<uint8_t>& protectedKey)
{
    std::vector<uint8_t> passwdBytes(password.length() * 2);
    for (unsigned i = 0, j = 0; i < password.length(); i++) {
        passwdBytes[j++] = 0;
        passwdBytes[j++] = (uint8_t)password[i];
    }

    // Get the salt associated with this key (the first SALT_LEN bytes
    // protectedKey
    std::vector<uint8_t> salt(SALT_LEN);
    arraycopy8(protectedKey, 0, salt, 0, SALT_LEN);

    // Determine the number of digest rounds
    int encrKeyLen = protectedKey.size() - SALT_LEN - DIGEST_LEN;
    int numRounds = encrKeyLen / DIGEST_LEN;
    if ((encrKeyLen % DIGEST_LEN) != 0)
        numRounds++;

    // Get the encrypted key portion and store it in "encrKey"
    std::vector<uint8_t> encrKey(encrKeyLen);
    arraycopy8(protectedKey, SALT_LEN, encrKey, 0, encrKeyLen);

    // Set up the uint8_t array which will be XORed with "encrKey"
    std::vector<uint8_t> xorKey(encrKeyLen);

    SHA_CTX context;
    SHA1_Init(&context);

    uint8_t digest[DIGEST_LEN];
    arraycopy8(salt, 0, digest, 0, DIGEST_LEN);

    // Compute the digests, and store them in "xorKey"
    for (int i = 0, xorOffset = 0; i < numRounds;
         i++, xorOffset += DIGEST_LEN) {
        SHA1_Update(&context, &passwdBytes[0], passwdBytes.size());
        SHA1_Update(&context, &digest[0], DIGEST_LEN);
        SHA1_Final(&digest[0], &context);
        SHA1_Init(&context);
        // Copy the digest into "xorKey"
        if (i < numRounds - 1) {
            arraycopy8(digest, 0, xorKey, xorOffset, DIGEST_LEN);
        } else {
            arraycopy8(digest, 0, xorKey, xorOffset, xorKey.size() - xorOffset);
        }
    }

    // XOR "encrKey" with "xorKey", and store the result in "plainKey"
    std::vector<uint8_t> plainKey(encrKeyLen);
    for (int i = 0; i < encrKeyLen; i++) {
        plainKey[i] = (uint8_t)(encrKey[i] ^ xorKey[i]);
    }

    // Check the integrity of the recovered key by concatenating it with
    // the password, digesting the concatenation, and comparing the
    // result of the digest operation with the digest provided at the end
    // of 'protectedKey'. If the two digest values are
    // different, throw an exception.
    SHA1_Update(&context, &passwdBytes[0], passwdBytes.size());
    SHA1_Update(&context, &plainKey[0], plainKey.size());
    SHA1_Final(&digest[0], &context);

    for (int i = 0; i < DIGEST_LEN; i++) {
        if (digest[i] != protectedKey[SALT_LEN + encrKeyLen + i]) {
            throw key_exception("Key password incorrect");
        }
    }

    return plainKey;
}

KeyStore::KeyStore(const fs::path& name, const std::string& pass)
{
    load(name, pass);
}

bool KeyStore::load(const fs::path& name, const std::string& pass)
{
    if (name == "") {
        return load(fastzip_keystore, pass);
    } else {
        MemBuffer buf(name);
        if (buf.size() < 12)
            return false;
        return load(buf, pass);
    }
}

bool KeyStore::load(const std::vector<uint8_t>& keystore,
                    const std::string& pass)
{
    MemBuffer buf(keystore);
    if (buf.size() < 12)
        return false;
    return load(buf, pass);
}

bool KeyStore::load(MemBuffer& buf, const std::string& /*pass*/)
{
    auto magic = buf.read<uint32_t>();
    if (magic != 0xfeedfeed)
        return false;
    // auto version = buf.read<uint32_t>();
    // auto count = buf.read<uint32_t>();

    membuf = buf;
    return true;
}

std::vector<uint8_t> KeyStore::getKey(const std::string& pass,
                                      const std::string& name)
{
    std::vector<uint8_t> privateKey;

    membuf.reset();
    membuf.seek(8);
    auto count = membuf.read<uint32_t>();
    bool foundKey = false;
    for (unsigned i = 0; i < count; i++) {
        auto tag = membuf.read<uint32_t>();
        auto alias = membuf.readString();
        /* auto timestamp = */ membuf.read<uint64_t>();

        assert(tag == 1);

        auto length = membuf.read<uint32_t>();
        DER keyData = readDER(membuf.readBuffer(length).buffer());
        if (!foundKey && (name == "" || alias == name)) {
            for (const auto& kd : keyData) {
                if (kd.tag == 0x04) {
                    privateKey = kd.data;
                    foundKey = true;
                    break;
                }
            }
        }

        auto ccount = membuf.read<uint32_t>();
        auto certName = membuf.readString();

        for (unsigned j = 0; j < ccount; j++) {
            auto clen = membuf.read<uint32_t>();
            auto certBuf = membuf.readBuffer(clen);
            if (foundKey) {
                certificate = certBuf.buffer();
                DER certData = readDER(certificate);

                // TODO: This code didn't do anything, should probably break out
                // of outer loop!
                /* for (int i = 0; i < certData[0].size(); i++) { */
                /*     if (certData[0][i].tag == 0x30 && certData[0][i][0].tag
                 * == 0x31) { */
                /*         break; */
                /*     } */
                /* } */
            }
        }
        if (foundKey)
            break;
    }

    if (!foundKey)
        throw key_exception("Key not found");

    auto plainKey = recoverKey(pass, privateKey);

    DER keyData = readDER(plainKey);

    for (const auto& kd : keyData) {
        if (kd.tag == 0x04) {
            return kd.data;
        }
    }
    throw key_exception("Could not recover key");
}
