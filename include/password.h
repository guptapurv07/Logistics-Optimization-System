#ifndef PASSWORD_H
#define PASSWORD_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// --- PASSWORD HASHING ---
// Passwords are never stored as plain text. Each one is stretched with PBKDF2-HMAC-SHA256 (RFC 8018)
// and its own random salt, then saved as "pbkdf2_sha256$<iterations>$<salt hex>$<hash hex>".

constexpr std::uint32_t kPasswordIterations = 100000;

std::array<std::uint8_t, 32> sha256(const std::string& data);
std::vector<std::uint8_t> pbkdf2Sha256(const std::string& password, const std::string& salt,
                                       std::uint32_t iterations, std::size_t length);

std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& stored);

std::string toHex(const std::uint8_t* data, std::size_t size);

#endif
