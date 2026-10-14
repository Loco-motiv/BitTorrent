#include "Utils.h"

std::string SHA1(std::string_view data)
{
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    auto processBlock = [&](const uint8_t* block)
    {
        std::array<uint32_t, 80> w;

        for (int i = 0; i < 16; ++i)
        {
            w[i] = (block[i * 4 + 0] << 24) |
                   (block[i * 4 + 1] << 16) |
                   (block[i * 4 + 2] << 8) |
                   (block[i * 4 + 3] << 0);
        }

        for (int i = 16; i < 80; ++i)
        {
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = std::rotl(a, 5) + f + e + k + w[i];
            e             = d;
            d             = c;
            c             = std::rotl(b, 30);
            b             = a;
            a             = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    };

    uint64_t len    = data.size();
    uint64_t bitLen = len * 8;
    size_t offset   = 0;

    while (offset + 64 <= len)
    {
        processBlock(reinterpret_cast<const uint8_t*>(data.data() + offset));
        offset += 64;
    }

    std::array<uint8_t, 128> remainder{};
    size_t rem = len - offset;
    std::copy_n(data.data() + offset, rem, remainder.begin());
    remainder[rem]      = 0x80;
    int blocksToProcess = (rem < 56) ? 1 : 2;
    int lenPos          = blocksToProcess * 64 - 8;

    for (int i = 0; i < 8; ++i)
    {
        remainder[lenPos + i] = (bitLen >> (56 - 8 * i)) & 0xFF;
    }

    processBlock(remainder.data());
    if (blocksToProcess == 2)
    {
        processBlock(remainder.data() + 64);
    }

    std::string hash_result(20, '\0');
    auto putBytes = [&hash_result](int index, uint32_t value)
    {
        hash_result[index + 0] = (value >> 24) & 0xFF;
        hash_result[index + 1] = (value >> 16) & 0xFF;
        hash_result[index + 2] = (value >> 8) & 0xFF;
        hash_result[index + 3] = (value >> 0) & 0xFF;
    };

    putBytes(0, h0);
    putBytes(4, h1);
    putBytes(8, h2);
    putBytes(12, h3);
    putBytes(16, h4);

    return hash_result;
}

std::string URLEncode(std::string_view data)
{
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;
    for (char c : data)
    {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || uc == '-' || uc == '_' || uc == '.' || uc == '~')
        {
            encoded << uc;
        }
        else
        {
            encoded << std::uppercase;
            encoded << '%' << std::setw(2) << int(uc);
            encoded << std::nouppercase;
        }
    }
    return encoded.str();
}
