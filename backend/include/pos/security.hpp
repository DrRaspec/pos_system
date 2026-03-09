#pragma once

#include <cstddef>
#include <string>

namespace pos {

std::string generateHexToken(std::size_t bytes);
std::string generateHexSalt(std::size_t bytes);
std::string sha256Hex(const std::string& input);
std::string pbkdf2HashHex(const std::string& password,
                          const std::string& salt_hex,
                          int iterations,
                          std::size_t output_bytes = 32);
bool constantTimeEquals(const std::string& left, const std::string& right);
bool verifyPassword(const std::string& plain_password,
                    const std::string& salt_hex,
                    int iterations,
                    const std::string& expected_hash_hex);

}  // namespace pos