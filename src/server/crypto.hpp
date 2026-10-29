/*
 * This file is part of Condor.
 *
 * Condor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Condor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Condor.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2026 Ezequiel Alves. All rights reserved.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>

namespace condor::hub {

/**
 * @brief Standalone RFC 3174 SHA-1 hash computation.
 */
class SHA1 {
public:
    static std::vector<uint8_t> hash(const uint8_t* data, size_t len) {
        uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

        size_t padded_len = ((len + 8) / 64 + 1) * 64;
        std::vector<uint8_t> msg(padded_len, 0);
        if (len > 0) {
            std::memcpy(msg.data(), data, len);
        }
        msg[len] = 0x80;

        uint64_t bits = static_cast<uint64_t>(len) * 8;
        for (int i = 0; i < 8; ++i) {
            msg[padded_len - 1 - i] = static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
        }

        auto rol = [](uint32_t val, uint32_t shift) -> uint32_t {
            return (val << shift) | (val >> (32 - shift));
        };

        for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i) {
                w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                       (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                       (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
            }
            for (int i = 16; i < 80; ++i) {
                w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }

            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

            for (int i = 0; i < 80; ++i) {
                uint32_t f = 0, k = 0;
                if (i < 20) {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                } else if (i < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                } else if (i < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                uint32_t temp = rol(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rol(b, 30);
                b = a;
                a = temp;
            }

            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
        }

        std::vector<uint8_t> digest(20);
        for (int i = 0; i < 5; ++i) {
            digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
        }
        return digest;
    }

    static std::vector<uint8_t> hash(std::string_view str) {
        return hash(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }
};

/**
 * @brief Standalone RFC 4648 Base64 encoding and decoding.
 */
class Base64 {
public:
    static std::string encode(const uint8_t* data, size_t len) {
        static constexpr char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);

        for (size_t i = 0; i < len; i += 3) {
            uint32_t val = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < len) val |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < len) val |= static_cast<uint32_t>(data[i + 2]);

            out.push_back(kTable[(val >> 18) & 0x3F]);
            out.push_back(kTable[(val >> 12) & 0x3F]);
            out.push_back((i + 1 < len) ? kTable[(val >> 6) & 0x3F] : '=');
            out.push_back((i + 2 < len) ? kTable[val & 0x3F] : '=');
        }
        return out;
    }

    static std::string encode(const std::vector<uint8_t>& data) {
        return encode(data.data(), data.size());
    }

    static std::string encode(std::string_view str) {
        return encode(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }
};

/**
 * @brief Computes standard WebSocket handshake accept key:
 * Base64(SHA1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")).
 */
inline std::string compute_websocket_accept(std::string_view client_key) {
    static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = std::string(client_key) + std::string(kGuid);
    auto sha = SHA1::hash(concat);
    return Base64::encode(sha);
}

} // namespace condor::hub
