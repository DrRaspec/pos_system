#include "pos/security.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pos {
namespace {

std::string bytesToHex(const std::vector<unsigned char>& bytes) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    oss << std::setw(2) << static_cast<int>(byte);
  }
  return oss.str();
}

std::string bytesToHex(const unsigned char* data, std::size_t length) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < length; ++i) {
    oss << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

std::vector<unsigned char> hexToBytes(const std::string& hex) {
  if (hex.size() % 2 != 0) {
    throw std::runtime_error("Invalid hex input length.");
  }

  std::vector<unsigned char> out;
  out.reserve(hex.size() / 2);

  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const char high = hex[i];
    const char low = hex[i + 1];
    if (!std::isxdigit(static_cast<unsigned char>(high)) ||
        !std::isxdigit(static_cast<unsigned char>(low))) {
      throw std::runtime_error("Invalid hex input value.");
    }

    const unsigned char byte = static_cast<unsigned char>(std::stoul(hex.substr(i, 2), nullptr, 16));
    out.push_back(byte);
  }

  return out;
}

std::string generateHexRandom(std::size_t bytes) {
  std::vector<unsigned char> data(bytes);
  if (RAND_bytes(data.data(), static_cast<int>(data.size())) != 1) {
    throw std::runtime_error("Unable to generate secure random bytes.");
  }
  return bytesToHex(data);
}

}  // namespace

std::string generateHexToken(std::size_t bytes) {
  return generateHexRandom(bytes);
}

std::string generateHexSalt(std::size_t bytes) {
  return generateHexRandom(bytes);
}

std::string sha256Hex(const std::string& input) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
  return bytesToHex(digest, SHA256_DIGEST_LENGTH);
}

std::string pbkdf2HashHex(const std::string& password,
                          const std::string& salt_hex,
                          int iterations,
                          std::size_t output_bytes) {
  if (iterations <= 0) {
    throw std::runtime_error("PBKDF2 iterations must be positive.");
  }

  const auto salt = hexToBytes(salt_hex);
  std::vector<unsigned char> output(output_bytes);

  const int ok = PKCS5_PBKDF2_HMAC(password.c_str(),
                                   static_cast<int>(password.size()),
                                   salt.data(),
                                   static_cast<int>(salt.size()),
                                   iterations,
                                   EVP_sha256(),
                                   static_cast<int>(output.size()),
                                   output.data());

  if (ok != 1) {
    throw std::runtime_error("PBKDF2 hashing failed.");
  }

  return bytesToHex(output);
}

bool constantTimeEquals(const std::string& left, const std::string& right) {
  if (left.size() != right.size()) {
    return false;
  }
  return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

bool verifyPassword(const std::string& plain_password,
                    const std::string& salt_hex,
                    int iterations,
                    const std::string& expected_hash_hex) {
  const auto calculated = pbkdf2HashHex(plain_password, salt_hex, iterations);
  return constantTimeEquals(calculated, expected_hash_hex);
}

}  // namespace pos