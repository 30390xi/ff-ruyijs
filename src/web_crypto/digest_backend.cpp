#ifndef _WIN32
#include "web_crypto/digest_backend.h"
#include <CommonCrypto/CommonDigest.h>
#include <cstdint>
#include <limits>
namespace ruyijs::web_crypto {
bool ComputeDigest(HashAlgorithm algorithm, const void* input, size_t length,
                   DigestBytes* output) noexcept {
  if (!output || length > std::numeric_limits<uint32_t>::max()) return false;
  const auto* data = static_cast<const uint8_t*>(input ? input : "");
  const auto size = CC_LONG(length);
  switch (algorithm) {
    case HashAlgorithm::Sha1:
      CC_SHA1(data, size, output->data); output->size = CC_SHA1_DIGEST_LENGTH; return true;
    case HashAlgorithm::Sha256:
      CC_SHA256(data, size, output->data); output->size = CC_SHA256_DIGEST_LENGTH; return true;
    case HashAlgorithm::Sha384:
      CC_SHA384(data, size, output->data); output->size = CC_SHA384_DIGEST_LENGTH; return true;
    case HashAlgorithm::Sha512:
      CC_SHA512(data, size, output->data); output->size = CC_SHA512_DIGEST_LENGTH; return true;
  }
  return false;
}
}
#else
#include "web_crypto/digest_backend.h"
#include <windows.h>
#include <bcrypt.h>
#include <limits>
namespace ruyijs::web_crypto {
bool ComputeDigest(HashAlgorithm algorithm, const void* input, size_t length,
                    DigestBytes* output) noexcept {
  static const wchar_t* const names[] = {
    BCRYPT_SHA1_ALGORITHM, BCRYPT_SHA256_ALGORITHM, BCRYPT_SHA384_ALGORITHM, BCRYPT_SHA512_ALGORITHM};
  static const size_t sizes[] = {20, 32, 48, 64};
  if (length > std::numeric_limits<ULONG>::max()) return false;
  BCRYPT_ALG_HANDLE provider = nullptr;
  unsigned index = unsigned(algorithm);
  if (BCryptOpenAlgorithmProvider(&provider, names[index], nullptr, 0) < 0) return false;
  NTSTATUS status = BCryptHash(provider, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<void*>(input)), ULONG(length), output->data,
      ULONG(sizes[index]));
  BCryptCloseAlgorithmProvider(provider, 0);
  if (status < 0) return false;
  output->size = sizes[index];
  return true;
}
}

#endif
