#include "../include/password.h"
#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>

namespace {

// --- SHA-256 (FIPS 180-4) ---
const std::uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

class Sha256 {
public:
    void update(const std::uint8_t* data, std::size_t size) {
        totalBytes_ += size;
        if (bufferSize_ > 0) {
            std::size_t take = std::min(size, sizeof(buffer_) - bufferSize_);
            std::memcpy(buffer_ + bufferSize_, data, take);
            bufferSize_ += take;
            data += take;
            size -= take;
            if (bufferSize_ < sizeof(buffer_)) return;
            compress(buffer_);
            bufferSize_ = 0;
        }
        for (; size >= 64; data += 64, size -= 64) compress(data);
        if (size > 0) {
            std::memcpy(buffer_, data, size);
            bufferSize_ = size;
        }
    }

    std::array<std::uint8_t, 32> finish() {
        std::uint64_t bitLength = totalBytes_ * 8;
        // Pad with 0x80 then zeros up to 56 bytes into a block, then the 8-byte message length
        std::uint8_t padding[64] = {0x80};
        update(padding, bufferSize_ < 56 ? 56 - bufferSize_ : 120 - bufferSize_);
        std::uint8_t lengthBytes[8];
        for (int i = 0; i < 8; ++i) lengthBytes[i] = std::uint8_t(bitLength >> (56 - 8 * i));
        update(lengthBytes, 8);

        std::array<std::uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 4; ++j) digest[i * 4 + j] = std::uint8_t(state_[i] >> (24 - 8 * j));
        }
        return digest;
    }

private:
    void compress(const std::uint8_t* block) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = std::uint32_t(block[i * 4]) << 24 | std::uint32_t(block[i * 4 + 1]) << 16 |
                   std::uint32_t(block[i * 4 + 2]) << 8 | std::uint32_t(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                               kRoundConstants[i] + w[i];
            std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t buffer_[64] = {};
    std::size_t bufferSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

// --- HMAC-SHA256 (RFC 2104) ---
// The padded key is hashed once up front; each compute() copies those two states,
// which roughly halves the work PBKDF2 does per iteration.
class HmacSha256 {
public:
    explicit HmacSha256(const std::string& key) {
        std::uint8_t block[64] = {};
        if (key.size() > sizeof(block)) {
            std::array<std::uint8_t, 32> digest = sha256(key);
            std::memcpy(block, digest.data(), digest.size());
        } else {
            std::memcpy(block, key.data(), key.size());
        }
        std::uint8_t innerPad[64], outerPad[64];
        for (int i = 0; i < 64; ++i) {
            innerPad[i] = block[i] ^ 0x36;
            outerPad[i] = block[i] ^ 0x5c;
        }
        inner_.update(innerPad, sizeof(innerPad));
        outer_.update(outerPad, sizeof(outerPad));
    }

    std::array<std::uint8_t, 32> compute(const std::uint8_t* data, std::size_t size) const {
        Sha256 inner = inner_;
        inner.update(data, size);
        std::array<std::uint8_t, 32> innerDigest = inner.finish();
        Sha256 outer = outer_;
        outer.update(innerDigest.data(), innerDigest.size());
        return outer.finish();
    }

private:
    Sha256 inner_, outer_;
};

const char kPrefix[] = "pbkdf2_sha256";

bool fromHex(const std::string& hex, std::string& out) {
    if (hex.size() % 2 != 0) return false;
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int high = value(hex[i]), low = value(hex[i + 1]);
        if (high < 0 || low < 0) return false;
        out += char(high * 16 + low);
    }
    return true;
}

std::string encode(const std::string& password, const std::string& salt, std::uint32_t iterations) {
    std::vector<std::uint8_t> hash = pbkdf2Sha256(password, salt, iterations, 32);
    return std::string(kPrefix) + "$" + std::to_string(iterations) + "$" +
           toHex(reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size()) + "$" +
           toHex(hash.data(), hash.size());
}

}  // namespace

std::array<std::uint8_t, 32> sha256(const std::string& data) {
    Sha256 hasher;
    hasher.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    return hasher.finish();
}

std::vector<std::uint8_t> pbkdf2Sha256(const std::string& password, const std::string& salt,
                                       std::uint32_t iterations, std::size_t length) {
    if (iterations == 0) throw std::invalid_argument("PBKDF2 needs at least one iteration");
    HmacSha256 hmac(password);
    std::vector<std::uint8_t> out;
    out.reserve(length);

    for (std::uint32_t blockIndex = 1; out.size() < length; ++blockIndex) {
        // U1 = HMAC(password, salt || blockIndex as 4 big-endian bytes)
        std::string first = salt;
        for (int shift = 24; shift >= 0; shift -= 8) first += char((blockIndex >> shift) & 0xFF);
        std::array<std::uint8_t, 32> u = hmac.compute(reinterpret_cast<const std::uint8_t*>(first.data()), first.size());
        std::array<std::uint8_t, 32> block = u;

        // Each later U is the HMAC of the one before; the block is all of them XOR-ed together
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = hmac.compute(u.data(), u.size());
            for (std::size_t j = 0; j < block.size(); ++j) block[j] ^= u[j];
        }
        std::size_t take = std::min(block.size(), length - out.size());
        out.insert(out.end(), block.begin(), block.begin() + take);
    }
    return out;
}

std::string hashPassword(const std::string& password) {
    std::random_device random;
    std::string salt;
    for (int i = 0; i < 4; ++i) {
        std::uint32_t bits = random();
        for (int shift = 0; shift < 32; shift += 8) salt += char((bits >> shift) & 0xFF);
    }
    return encode(password, salt, kPasswordIterations);
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    // Expected shape: pbkdf2_sha256$<iterations>$<salt hex>$<hash hex>
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t pos; (pos = stored.find('$', start)) != std::string::npos; start = pos + 1) {
        parts.push_back(stored.substr(start, pos - start));
    }
    parts.push_back(stored.substr(start));
    if (parts.size() != 4 || parts[0] != kPrefix || parts[1].empty() || parts[1].size() > 8 ||
        !std::all_of(parts[1].begin(), parts[1].end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    std::uint32_t iterations = std::uint32_t(std::stoul(parts[1]));
    std::string salt, expected;
    if (iterations == 0 || !fromHex(parts[2], salt) || !fromHex(parts[3], expected)) return false;

    std::vector<std::uint8_t> actual = pbkdf2Sha256(password, salt, iterations, expected.size());
    // Compare every byte, so the time taken doesn't reveal how much of the hash matched
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) difference |= actual[i] ^ std::uint8_t(expected[i]);
    return !expected.empty() && difference == 0;
}

std::string toHex(const std::uint8_t* data, std::size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        hex += digits[data[i] >> 4];
        hex += digits[data[i] & 0x0F];
    }
    return hex;
}
