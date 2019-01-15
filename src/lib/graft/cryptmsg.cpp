#include "lib/graft/cryptmsg.h"
#include "crypto/chacha.h"

namespace graft::cryptmsg {

inline size_t getEncryptChachaSize(size_t plainSize)
{
    return plainSize + sizeof(crypto::chacha_iv);
}

inline size_t getDecryptChachaSize(size_t cipherSize)
{
    assert(sizeof(crypto::chacha_iv) < cipherSize);
    return cipherSize - sizeof(crypto::chacha_iv);
}

void encryptChacha(const void* plain, size_t plain_size, const crypto::secret_key &skey, void* cipher)
{
  crypto::chacha_key key;
  crypto::generate_chacha_key(&skey, sizeof(skey), key);
  crypto::chacha_iv& iv = *reinterpret_cast<crypto::chacha_iv*>(cipher);
  iv = crypto::rand<crypto::chacha_iv>();
  crypto::chacha8(plain, plain_size, key, iv, reinterpret_cast<char*>(cipher) + sizeof(iv));
}

void decryptChacha(const void* cipher, size_t cipher_size, const crypto::secret_key &skey, void* plain)
{
  const size_t prefix_size = sizeof(crypto::chacha_iv);
  crypto::chacha_key key;
  crypto::generate_chacha_key(&skey, sizeof(skey), key);
  const crypto::chacha_iv &iv = *reinterpret_cast<const crypto::chacha_iv*>(cipher);
  crypto::chacha8(reinterpret_cast<const char*>(cipher) + sizeof(iv), cipher_size - prefix_size, key, iv, reinterpret_cast<char*>(plain));
}

constexpr uint16_t cStart = 0xA5A5;
constexpr uint16_t cEnd = 0x5A5A;

#pragma pack(push, 1)

//sessionX in decrypted form contains session key x and constants to check that the decryption was correct
struct sessionX
{
    uint16_t cstart; //decrypted value cStart
    crypto::secret_key x;
    uint16_t cend; //decrypted value cEnd
};

struct xEntry
{
    uint32_t Bhash; //xor of B
    uint8_t cipherX[sizeof(sessionX) + sizeof(crypto::chacha_iv)]; //encrypted sessionX
};

struct cryptoMessageHead
{
    uint32_t plainSize; //size of decrypted data
    crypto::public_key R; //random key used to encrypt session key
    uint16_t count; //recipients count
    xEntry xentries[1];
};

#pragma pack(pop)

uint32_t getBhash(const crypto::public_key& B)
{
    uint32_t res = 0;
    static_assert(sizeof(B) % sizeof(res) == 0);
    const uint32_t* p = reinterpret_cast<const uint32_t*>(&B);
    for(int i = 0, cnt = sizeof(B) / sizeof(res); i < cnt; ++i, ++p)
    {
        res ^= *p;
    }
    return res;
}

void encryptMessage(const std::string data, const crypto::public_key* Bkeys, size_t BkeysCount, std::string& message)
{
    assert(0<BkeysCount);
    //make decorated session key
    sessionX X; X.cstart = cStart; X.cend = cEnd;
    //generate session key X.x
    {
        crypto::public_key tmpX;
        crypto::generate_keys(tmpX,X.x);
    }
    //prepare
    size_t msgHeadSize = sizeof(cryptoMessageHead) + (BkeysCount - 1) * sizeof(xEntry);
    size_t msgSize = msgHeadSize + getEncryptChachaSize(data.size());
    message.resize(msgSize);
    cryptoMessageHead& head = *reinterpret_cast<cryptoMessageHead*>(message.data());
    head.plainSize = data.size();
    crypto::secret_key r;
    crypto::generate_keys(head.R, r);
    head.count = BkeysCount;
    //chacha encrypt with x
    encryptChacha(data.data(), data.size(), X.x, message.data()+msgHeadSize);
    //fill xEntry for each B
    const crypto::public_key* pB = Bkeys;
    xEntry* pxe = head.xentries;
    for(int i=0; i<BkeysCount; ++i, ++pxe, ++pB)
    {
        const crypto::public_key& B = *pB;
        xEntry& xe = *pxe;
        xe.Bhash = getBhash(B);
        //get rB key
        crypto::key_derivation rBv;
        crypto::generate_key_derivation(B, r, rBv);
        crypto::secret_key rB;
        crypto::derivation_to_scalar(rBv, 0, rB);
        //encrypt X with rB key
        encryptChacha(&X, sizeof(X), rB, xe.cipherX);
    }
}

bool decryptMessage(const std::string message, const crypto::secret_key& bkey, std::string& data)
{
    if(message.size() <= sizeof(cryptoMessageHead)) return false;
    //prepare
    const cryptoMessageHead& head = *reinterpret_cast<const cryptoMessageHead*>(message.data());
    size_t msgHeadSize = sizeof(cryptoMessageHead) + ((size_t)(head.count - 1)) * sizeof(xEntry);
    size_t msgSize = msgHeadSize + getEncryptChachaSize(head.plainSize);
    if(message.size() < msgSize) return false;
    //get Bhash from b
    const crypto::secret_key& b = bkey;
    uint32_t Bhash;
    {
        crypto::public_key B;
        bool res = crypto::secret_key_to_public_key(bkey, B);
        assert(res);
        Bhash = getBhash(B);
    }
    //find xEntry for each B
    const xEntry* pxe = head.xentries;
    for(int i=0; i<head.count; ++i, ++pxe)
    {
        const xEntry& xe = *pxe;
        if(xe.Bhash != Bhash) continue;
        //get bR key
        crypto::key_derivation bRv;
        crypto::generate_key_derivation(head.R, b, bRv);
        crypto::secret_key bR;
        crypto::derivation_to_scalar(bRv, 0, bR);
        //decrypt to X
        sessionX X;
        decryptChacha(xe.cipherX, sizeof(xe.cipherX), bR, &X);
        if(X.cstart != cStart || X.cend != cEnd) continue;
        //decrypt with session key
        data.resize(head.plainSize);
        decryptChacha(message.data() + msgHeadSize, getEncryptChachaSize(head.plainSize), X.x, data.data());
        return true;
    }
    return false;
}

} //namespace graft::cryptmsg

