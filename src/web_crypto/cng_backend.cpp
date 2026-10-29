#ifndef _WIN32
#include "web_crypto/cng_backend.h"
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonHMAC.h>
#include <Security/Security.h>
#include <cstddef>
#include <cstring>

// libsystem exports these, and the macOS 26 SDK headers do not declare them.
extern "C" CCCryptorStatus CCCryptorGCMOneshotEncrypt(
    CCAlgorithm alg, const void* key, size_t keyLength, const void* iv, size_t ivLength,
    const void* aData, size_t aDataLength, const void* dataIn, size_t dataInLength,
    void* cipherOut, void* tagOut, size_t tagLength);
extern "C" CCCryptorStatus CCCryptorGCMOneshotDecrypt(
    CCAlgorithm alg, const void* key, size_t keyLength, const void* iv, size_t ivLength,
    const void* aData, size_t aDataLength, const void* dataIn, size_t dataInLength,
    void* dataOut, const void* tag, size_t tagLength);

namespace ruyijs::web_crypto {
namespace {
namespace ec = ruyijs::domain::crypto;

bool AesKey(const Bytes& key) { return key.size() == 16 || key.size() == 24 || key.size() == 32; }
int CurveBits(const ec::EcCurve& curve) {
  if (curve.size == 32) return 256;
  if (curve.size == 48) return 384;
  if (curve.size == 66) return 521;
  return 0;
}
CFMutableDictionaryRef KeyAttrs(const ec::EcCurve& curve, bool priv) {
  int bits = CurveBits(curve);
  CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bits);
  CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(attrs, kSecAttrKeyType, kSecAttrKeyTypeECSECPrimeRandom);
  CFDictionarySetValue(attrs, kSecAttrKeySizeInBits, number);
  CFDictionarySetValue(attrs, kSecAttrKeyClass, priv ? kSecAttrKeyClassPrivate : kSecAttrKeyClassPublic);
  CFRelease(number);
  return attrs;
}
void Wipe(Bytes* bytes) {
  volatile uint8_t* cursor = bytes->data();
  for (size_t i = 0; i < bytes->size(); ++i) cursor[i] = 0;
}
SecKeyRef ImportPoint(const ec::EcKey& key, bool priv) {
  if (!key.curve || !CurveBits(*key.curve)) return nullptr;
  const size_t n = key.curve->size;
  if (key.x.size() != n || key.y.size() != n || (priv && key.d.size() != n)) return nullptr;
  Bytes blob;
  blob.reserve(1 + (priv ? 3 : 2) * n);
  blob.push_back(0x04);
  blob.insert(blob.end(), key.x.begin(), key.x.end());
  blob.insert(blob.end(), key.y.begin(), key.y.end());
  if (priv) blob.insert(blob.end(), key.d.begin(), key.d.end());
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, blob.data(), blob.size());
  CFMutableDictionaryRef attrs = KeyAttrs(*key.curve, priv);
  CFErrorRef error = nullptr;
  SecKeyRef sec = SecKeyCreateWithData(data, attrs, &error);
  if (error) CFRelease(error);
  CFRelease(data);
  CFRelease(attrs);
  Wipe(&blob);
  return sec;
}
bool SamePublic(SecKeyRef priv, const ec::EcKey& key) {
  SecKeyRef pub = SecKeyCopyPublicKey(priv);
  if (!pub) return false;
  CFErrorRef error = nullptr;
  CFDataRef data = SecKeyCopyExternalRepresentation(pub, &error);
  if (error) CFRelease(error);
  const size_t n = key.curve->size;
  bool ok = data && size_t(CFDataGetLength(data)) == 1 + 2 * n && CFDataGetBytePtr(data)[0] == 0x04 &&
      memcmp(CFDataGetBytePtr(data) + 1, key.x.data(), n) == 0 &&
      memcmp(CFDataGetBytePtr(data) + 1 + n, key.y.data(), n) == 0;
  if (data) CFRelease(data);
  CFRelease(pub);
  return ok;
}
SecKeyAlgorithm DigestAlgorithm(HashAlgorithm hash) {
  switch (hash) {
    case HashAlgorithm::Sha1: return kSecKeyAlgorithmECDSASignatureDigestRFC4754SHA1;
    case HashAlgorithm::Sha256: return kSecKeyAlgorithmECDSASignatureDigestRFC4754SHA256;
    case HashAlgorithm::Sha384: return kSecKeyAlgorithmECDSASignatureDigestRFC4754SHA384;
    case HashAlgorithm::Sha512: return kSecKeyAlgorithmECDSASignatureDigestRFC4754SHA512;
  }
  return kSecKeyAlgorithmECDSASignatureDigestRFC4754SHA256;
}
}  // namespace

bool AesCbc(const Bytes& key, const Bytes& iv, const Bytes& input, bool encrypt, Bytes* out) {
  if (!AesKey(key) || iv.size() != 16) return false;
  out->assign(input.size() + 16, 0);
  size_t moved = 0;
  const uint8_t empty = 0;
  CCCryptorStatus status = CCCrypt(encrypt ? kCCEncrypt : kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding,
      key.data(), key.size(), iv.data(), input.empty() ? &empty : input.data(), input.size(),
      out->data(), out->size(), &moved);
  if (status != kCCSuccess) return false;
  out->resize(moved);
  return true;
}

bool AesGcm(const Bytes& key, const Bytes& iv, const Bytes& aad, size_t tagBytes,
            const Bytes& input, bool encrypt, Bytes* out) {
  if (!AesKey(key) || iv.size() != 12 || tagBytes < 4 || tagBytes > 16) return false;
  if (!encrypt && input.size() < tagBytes) return false;
  const size_t body = encrypt ? input.size() : input.size() - tagBytes;
  Bytes tag = encrypt ? Bytes(tagBytes, 0) : Bytes(input.begin() + ptrdiff_t(body), input.end());
  Bytes result(body ? body : 1, 0);
  uint8_t sink = 0;
  const uint8_t empty = 0;
  const void* source = body ? input.data() : &empty;
  void* target = body ? result.data() : &sink;
  CCCryptorStatus status = encrypt
      ? CCCryptorGCMOneshotEncrypt(kCCAlgorithmAES, key.data(), key.size(), iv.data(), iv.size(),
            aad.empty() ? nullptr : aad.data(), aad.size(), source, body, target, tag.data(), tag.size())
      : CCCryptorGCMOneshotDecrypt(kCCAlgorithmAES, key.data(), key.size(), iv.data(), iv.size(),
            aad.empty() ? nullptr : aad.data(), aad.size(), source, body, target, tag.data(), tag.size());
  if (status != kCCSuccess) return false;
  result.resize(body);
  if (encrypt) result.insert(result.end(), tag.begin(), tag.end());
  *out = std::move(result);
  return true;
}

bool Hmac(HashAlgorithm hash, const Bytes& key, const Bytes& data, Bytes* out) {
  const CCHmacAlgorithm algs[] = {kCCHmacAlgSHA1, kCCHmacAlgSHA256, kCCHmacAlgSHA384, kCCHmacAlgSHA512};
  const size_t sizes[] = {20, 32, 48, 64};
  const unsigned index = unsigned(hash);
  if (index > 3) return false;
  out->assign(sizes[index], 0);
  const uint8_t empty = 0;
  CCHmac(algs[index], key.empty() ? &empty : key.data(), key.size(),
         data.empty() ? &empty : data.data(), data.size(), out->data());
  return true;
}

bool EcGenerate(const ec::EcCurve& curve, bool ecdh, ec::EcKey* out) {
  (void)ecdh;
  if (!CurveBits(curve)) return false;
  CFMutableDictionaryRef attrs = KeyAttrs(curve, true);
  CFErrorRef error = nullptr;
  SecKeyRef priv = SecKeyCreateRandomKey(attrs, &error);
  if (error) CFRelease(error);
  CFRelease(attrs);
  if (!priv) return false;
  error = nullptr;
  CFDataRef blob = SecKeyCopyExternalRepresentation(priv, &error);
  if (error) CFRelease(error);
  CFRelease(priv);
  const size_t n = curve.size;
  if (!blob || size_t(CFDataGetLength(blob)) != 1 + 3 * n || CFDataGetBytePtr(blob)[0] != 0x04) {
    if (blob) CFRelease(blob);
    return false;
  }
  const UInt8* bytes = CFDataGetBytePtr(blob);
  out->curve = &curve;
  out->x.assign(bytes + 1, bytes + 1 + n);
  out->y.assign(bytes + 1 + n, bytes + 1 + 2 * n);
  out->d.assign(bytes + 1 + 2 * n, bytes + 1 + 3 * n);
  CFRelease(blob);
  return true;
}

bool EcValidate(const ec::EcKey& key, bool ecdh) {
  (void)ecdh;
  if (!key.curve) return false;
  SecKeyRef pub = ImportPoint(key, false);
  if (!pub) return false;
  CFRelease(pub);
  if (key.d.empty()) return true;
  SecKeyRef priv = ImportPoint(key, true);
  if (!priv) return false;
  bool ok = SamePublic(priv, key);
  CFRelease(priv);
  return ok;
}

bool EcdsaSign(const ec::EcKey& key, HashAlgorithm hash, const Bytes& message, Bytes* out) {
  DigestBytes digest;
  SecKeyRef priv = ImportPoint(key, true);
  if (!priv || !ComputeDigest(hash, message.data(), message.size(), &digest)) {
    if (priv) CFRelease(priv);
    return false;
  }
  CFDataRef hashed = CFDataCreate(kCFAllocatorDefault, digest.data, digest.size);
  CFErrorRef error = nullptr;
  CFDataRef sig = SecKeyCreateSignature(priv, DigestAlgorithm(hash), hashed, &error);
  if (error) CFRelease(error);
  CFRelease(hashed);
  CFRelease(priv);
  const size_t n = key.curve->size;
  if (!sig || size_t(CFDataGetLength(sig)) != 2 * n) {
    if (sig) CFRelease(sig);
    return false;
  }
  const UInt8* bytes = CFDataGetBytePtr(sig);
  out->assign(bytes, bytes + 2 * n);
  CFRelease(sig);
  return true;
}

bool EcdsaVerify(const ec::EcKey& key, HashAlgorithm hash, const Bytes& message,
                 const Bytes& signature, bool* valid) {
  DigestBytes digest;
  ec::EcKey pub{key.curve, key.x, key.y, {}};
  SecKeyRef sec = ImportPoint(pub, false);
  if (!sec || !ComputeDigest(hash, message.data(), message.size(), &digest)) {
    if (sec) CFRelease(sec);
    return false;
  }
  if (signature.size() != 2 * key.curve->size) {
    CFRelease(sec);
    *valid = false;
    return true;
  }
  CFDataRef hashed = CFDataCreate(kCFAllocatorDefault, digest.data, digest.size);
  CFDataRef sig = CFDataCreate(kCFAllocatorDefault, signature.data(), signature.size());
  CFErrorRef error = nullptr;
  *valid = SecKeyVerifySignature(sec, DigestAlgorithm(hash), hashed, sig, &error);
  if (error) CFRelease(error);
  CFRelease(sig);
  CFRelease(hashed);
  CFRelease(sec);
  return true;
}

bool EcdhSecret(const ec::EcKey& privateKey, const ec::EcKey& publicKey, Bytes* out) {
  if (!privateKey.curve || privateKey.curve != publicKey.curve) return false;
  SecKeyRef mine = ImportPoint(privateKey, true);
  ec::EcKey peer{publicKey.curve, publicKey.x, publicKey.y, {}};
  SecKeyRef theirs = ImportPoint(peer, false);
  if (!mine || !theirs) {
    if (mine) CFRelease(mine);
    if (theirs) CFRelease(theirs);
    return false;
  }
  CFMutableDictionaryRef params = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFErrorRef error = nullptr;
  CFDataRef secret = SecKeyCopyKeyExchangeResult(
      mine, kSecKeyAlgorithmECDHKeyExchangeStandard, theirs, params, &error);
  if (error) CFRelease(error);
  CFRelease(params);
  CFRelease(mine);
  CFRelease(theirs);
  const size_t n = privateKey.curve->size;
  if (!secret || size_t(CFDataGetLength(secret)) > n) {
    if (secret) CFRelease(secret);
    return false;
  }
  const size_t got = size_t(CFDataGetLength(secret));
  out->assign(n - got, 0);
  out->insert(out->end(), CFDataGetBytePtr(secret), CFDataGetBytePtr(secret) + got);
  CFRelease(secret);
  return true;
}
}  // namespace ruyijs::web_crypto

#else
#include "web_crypto/cng_backend.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <limits>
namespace ruyijs::web_crypto {
namespace {
namespace ec = ruyijs::domain::crypto;
struct Provider {
  BCRYPT_ALG_HANDLE handle = nullptr;
  explicit Provider(const wchar_t* name) { if (BCryptOpenAlgorithmProvider(&handle, name, nullptr, 0) < 0) handle = nullptr; }
  ~Provider() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Key {
  BCRYPT_KEY_HANDLE handle = nullptr;
  ~Key() { if (handle) BCryptDestroyKey(handle); }
};
bool Fits(size_t n) { return n <= std::numeric_limits<ULONG>::max(); }
const wchar_t* EcAlgorithm(const ec::EcCurve& curve, bool ecdh) {
  if (curve.size == 32) return ecdh ? BCRYPT_ECDH_P256_ALGORITHM : BCRYPT_ECDSA_P256_ALGORITHM;
  if (curve.size == 48) return ecdh ? BCRYPT_ECDH_P384_ALGORITHM : BCRYPT_ECDSA_P384_ALGORITHM;
  return ecdh ? BCRYPT_ECDH_P521_ALGORITHM : BCRYPT_ECDSA_P521_ALGORITHM;
}
ULONG EcMagic(const ec::EcCurve& curve, bool ecdh, bool priv) {
  if (curve.size == 32) return ecdh ? (priv ? BCRYPT_ECDH_PRIVATE_P256_MAGIC : BCRYPT_ECDH_PUBLIC_P256_MAGIC)
                                    : (priv ? BCRYPT_ECDSA_PRIVATE_P256_MAGIC : BCRYPT_ECDSA_PUBLIC_P256_MAGIC);
  if (curve.size == 48) return ecdh ? (priv ? BCRYPT_ECDH_PRIVATE_P384_MAGIC : BCRYPT_ECDH_PUBLIC_P384_MAGIC)
                                    : (priv ? BCRYPT_ECDSA_PRIVATE_P384_MAGIC : BCRYPT_ECDSA_PUBLIC_P384_MAGIC);
  return ecdh ? (priv ? BCRYPT_ECDH_PRIVATE_P521_MAGIC : BCRYPT_ECDH_PUBLIC_P521_MAGIC)
              : (priv ? BCRYPT_ECDSA_PRIVATE_P521_MAGIC : BCRYPT_ECDSA_PUBLIC_P521_MAGIC);
}
// BCRYPT_ECCKEY_BLOB: magic, field size, then X, Y and (private) d.
bool ImportEc(const Provider& provider, const ec::EcKey& key, bool ecdh, bool priv, Key* out) {
  const size_t n = key.curve->size;
  if (key.x.size() != n || key.y.size() != n || (priv && key.d.size() != n)) return false;
  Bytes blob(sizeof(BCRYPT_ECCKEY_BLOB));
  auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
  header->dwMagic = EcMagic(*key.curve, ecdh, priv);
  header->cbKey = ULONG(n);
  blob.insert(blob.end(), key.x.begin(), key.x.end());
  blob.insert(blob.end(), key.y.begin(), key.y.end());
  if (priv) blob.insert(blob.end(), key.d.begin(), key.d.end());
  return provider.handle && BCryptImportKeyPair(provider.handle, nullptr,
      priv ? BCRYPT_ECCPRIVATE_BLOB : BCRYPT_ECCPUBLIC_BLOB, &out->handle, blob.data(), ULONG(blob.size()), 0) >= 0;
}
bool Symmetric(const wchar_t* mode, const Bytes& key, Provider* provider, Key* out) {
  if (!provider->handle || (key.size() != 16 && key.size() != 24 && key.size() != 32)) return false;
  if (BCryptSetProperty(provider->handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
                        ULONG((wcslen(mode) + 1) * sizeof(wchar_t)), 0) < 0) return false;
  return BCryptGenerateSymmetricKey(provider->handle, &out->handle, nullptr, 0,
                                    const_cast<PUCHAR>(key.data()), ULONG(key.size()), 0) >= 0;
}
}  // namespace

bool AesCbc(const Bytes& key, const Bytes& iv, const Bytes& input, bool encrypt, Bytes* out) {
  Provider provider(BCRYPT_AES_ALGORITHM); Key handle;
  if (iv.size() != 16 || !Fits(input.size() + 16) || !Symmetric(BCRYPT_CHAIN_MODE_CBC, key, &provider, &handle)) return false;
  ULONG size = 0;
  uint8_t empty = 0;  // CNG rejects a null input even for zero bytes
  PUCHAR source = input.empty() ? &empty : const_cast<PUCHAR>(input.data());
  auto run = [&](PUCHAR target, ULONG capacity) {
    Bytes local = iv;
    return (encrypt ? BCryptEncrypt : BCryptDecrypt)(handle.handle, source, ULONG(input.size()),
        nullptr, local.data(), ULONG(local.size()), target, capacity, &size, BCRYPT_BLOCK_PADDING) >= 0;
  };
  if (!run(nullptr, 0)) return false;
  out->assign(size, 0);
  if (!run(out->data(), ULONG(out->size()))) return false;
  out->resize(size);
  return true;
}

bool AesGcm(const Bytes& key, const Bytes& iv, const Bytes& aad, size_t tagBytes,
            const Bytes& input, bool encrypt, Bytes* out) {
  Provider provider(BCRYPT_AES_ALGORITHM); Key handle;
  // CNG's GCM takes a 96-bit nonce only.
  if (iv.size() != 12 || tagBytes < 4 || tagBytes > 16 || !Fits(input.size()) || !Fits(aad.size()) ||
      !Symmetric(BCRYPT_CHAIN_MODE_GCM, key, &provider, &handle)) return false;
  if (!encrypt && input.size() < tagBytes) return false;
  const size_t body = encrypt ? input.size() : input.size() - tagBytes;
  Bytes tag = encrypt ? Bytes(tagBytes, 0) : Bytes(input.begin() + body, input.end());
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(iv.data()); info.cbNonce = ULONG(iv.size());
  info.pbAuthData = aad.empty() ? nullptr : const_cast<PUCHAR>(aad.data()); info.cbAuthData = ULONG(aad.size());
  info.pbTag = tag.data(); info.cbTag = ULONG(tag.size());
  Bytes result(body ? body : 1);
  ULONG size = 0;
  NTSTATUS status = (encrypt ? BCryptEncrypt : BCryptDecrypt)(handle.handle, body ? const_cast<PUCHAR>(input.data()) : nullptr,
      ULONG(body), &info, nullptr, 0, result.data(), ULONG(body), &size, 0);
  if (status < 0) return false;
  result.resize(size);
  if (encrypt) result.insert(result.end(), tag.begin(), tag.end());
  *out = std::move(result);
  return true;
}

bool Hmac(HashAlgorithm hash, const Bytes& key, const Bytes& data, Bytes* out) {
  static const wchar_t* const names[] = {BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM, BCRYPT_SHA384_ALGORITHM, BCRYPT_SHA512_ALGORITHM};
  static const size_t sizes[] = {20, 32, 48, 64};
  BCRYPT_ALG_HANDLE provider = nullptr;
  if (!Fits(key.size()) || !Fits(data.size()) ||
      BCryptOpenAlgorithmProvider(&provider, names[unsigned(hash)], nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return false;
  uint8_t empty = 0;
  out->assign(sizes[unsigned(hash)], 0);
  NTSTATUS status = BCryptHash(provider, key.empty() ? &empty : const_cast<PUCHAR>(key.data()), ULONG(key.size()),
                               data.empty() ? &empty : const_cast<PUCHAR>(data.data()), ULONG(data.size()),
                               out->data(), ULONG(out->size()));
  BCryptCloseAlgorithmProvider(provider, 0);
  return status >= 0;
}

bool EcGenerate(const ec::EcCurve& curve, bool ecdh, ec::EcKey* out) {
  Provider provider(EcAlgorithm(curve, ecdh)); Key key;
  const ULONG bits = curve.size == 66 ? 521 : ULONG(curve.size * 8);
  if (!provider.handle || BCryptGenerateKeyPair(provider.handle, &key.handle, bits, 0) < 0 ||
      BCryptFinalizeKeyPair(key.handle, 0) < 0) return false;
  ULONG size = 0;
  if (BCryptExportKey(key.handle, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &size, 0) < 0) return false;
  Bytes blob(size);
  if (BCryptExportKey(key.handle, nullptr, BCRYPT_ECCPRIVATE_BLOB, blob.data(), size, &size, 0) < 0) return false;
  const size_t n = curve.size, at = sizeof(BCRYPT_ECCKEY_BLOB);
  if (size < at + 3 * n) return false;
  out->curve = &curve;
  out->x.assign(blob.begin() + at, blob.begin() + at + n);
  out->y.assign(blob.begin() + at + n, blob.begin() + at + 2 * n);
  out->d.assign(blob.begin() + at + 2 * n, blob.begin() + at + 3 * n);
  SecureZeroMemory(blob.data(), blob.size());
  return true;
}

bool EcValidate(const ec::EcKey& key, bool ecdh) {
  Provider provider(EcAlgorithm(*key.curve, ecdh)); Key handle;
  return ImportEc(provider, key, ecdh, !key.d.empty(), &handle);
}

bool EcdsaSign(const ec::EcKey& key, HashAlgorithm hash, const Bytes& message, Bytes* out) {
  Provider provider(EcAlgorithm(*key.curve, false)); Key handle;
  DigestBytes digest;
  if (!ComputeDigest(hash, message.data(), message.size(), &digest) || !ImportEc(provider, key, false, true, &handle)) return false;
  ULONG size = 0;
  if (BCryptSignHash(handle.handle, nullptr, digest.data, ULONG(digest.size), nullptr, 0, &size, 0) < 0) return false;
  out->assign(size, 0);
  return BCryptSignHash(handle.handle, nullptr, digest.data, ULONG(digest.size), out->data(), size, &size, 0) >= 0;
}

bool EcdsaVerify(const ec::EcKey& key, HashAlgorithm hash, const Bytes& message,
                 const Bytes& signature, bool* valid) {
  Provider provider(EcAlgorithm(*key.curve, false)); Key handle;
  DigestBytes digest;
  ec::EcKey publicKey{key.curve, key.x, key.y, {}};
  if (!ComputeDigest(hash, message.data(), message.size(), &digest) || !ImportEc(provider, publicKey, false, false, &handle)) return false;
  *valid = signature.size() == 2 * key.curve->size &&
      BCryptVerifySignature(handle.handle, nullptr, digest.data, ULONG(digest.size),
                            const_cast<PUCHAR>(signature.data()), ULONG(signature.size()), 0) >= 0;
  return true;
}

bool EcdhSecret(const ec::EcKey& privateKey, const ec::EcKey& publicKey, Bytes* out) {
  Provider provider(EcAlgorithm(*privateKey.curve, true)); Key mine, theirs;
  ec::EcKey peer{publicKey.curve, publicKey.x, publicKey.y, {}};
  if (privateKey.curve != publicKey.curve || !ImportEc(provider, privateKey, true, true, &mine) ||
      !ImportEc(provider, peer, true, false, &theirs)) return false;
  BCRYPT_SECRET_HANDLE secret = nullptr;
  if (BCryptSecretAgreement(mine.handle, theirs.handle, &secret, 0) < 0) return false;
  ULONG size = 0;
  bool ok = BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &size, 0) >= 0;
  if (ok) {
    out->assign(size, 0);
    ok = BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, out->data(), size, &size, 0) >= 0;
  }
  BCryptDestroySecret(secret);
  // KDF_RAW_SECRET is little-endian; WebCrypto's secret is the big-endian x.
  if (ok) std::reverse(out->begin(), out->end());
  return ok;
}
}  // namespace ruyijs::web_crypto

#endif
