#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace petrichor::tools
{

/** Writes interleaved stereo 24-bit PCM WAV. Returns false on I/O failure. */
inline bool writeWav24 (const std::string& path, const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;

    const uint32_t frames = (uint32_t) std::min (left.size(), right.size());
    const uint16_t channels = 2, bits = 24, blockAlign = channels * bits / 8;
    const uint32_t dataBytes = frames * blockAlign;

    auto u32 = [f] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto u16 = [f] (uint16_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };

    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (channels);
    u32 ((uint32_t) sampleRate); u32 ((uint32_t) sampleRate * blockAlign); u16 (blockAlign); u16 (bits);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);

    std::vector<uint8_t> bytes ((size_t) dataBytes);
    size_t p = 0;
    for (uint32_t i = 0; i < frames; ++i)
    {
        for (const float s : { left[i], right[i] })
        {
            const float c = std::fmax (-1.0f, std::fmin (1.0f, s));
            const int32_t v = (int32_t) std::lrint (c * 8388607.0f);
            bytes[p++] = (uint8_t) v;
            bytes[p++] = (uint8_t) (v >> 8);
            bytes[p++] = (uint8_t) (v >> 16);
        }
    }
    const bool ok = std::fwrite (bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose (f);
    return ok;
}

} // namespace petrichor::tools
