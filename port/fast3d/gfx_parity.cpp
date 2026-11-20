/*
 * gfx_parity.cpp - pixel comparison for renderer parity mode
 * (--renderer-parity, port/fast3d/gfx_tee.cpp).
 *
 * When a smoke "screenshot" event fires in parity mode, the classic GL
 * renderer's back buffer is captured as usual (<path>) and bgfx's offscreen
 * copy of the same frame is read back. This compares the two images pixel
 * for pixel and writes, beside <path>:
 *
 *   <path>.bgfx.bmp     the bgfx image
 *   <path>.diff.bmp     black where equal; elsewhere the absolute channel
 *                       difference scaled by 64 (any difference shows)
 *   <path>.parity.json  sizes, differing pixel count, the largest channel
 *                       difference, a difference histogram and bounding box
 *
 * and logs one "PARITY:" line. Captures are written where the smoke harness
 * puts screenshots, outside git (BYOR).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "gfx_parity.h"
#include "system.h"

namespace {

/* Rows are bottom-up in both inputs, which is BMP's order too. */
bool writeBmp24(const std::string &path, int w, int h, const std::vector<uint8_t> &rgb)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "PARITY: could not write %s", path.c_str());
        return false;
    }
    const int rowsize = (w * 3 + 3) & ~3;
    const int imgsize = rowsize * h;
    const int filesize = 54 + imgsize;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    for (int i = 0; i < 4; i++) {
        hdr[2 + i] = (unsigned char)(filesize >> (8 * i));
        hdr[18 + i] = (unsigned char)(w >> (8 * i));
        hdr[22 + i] = (unsigned char)(h >> (8 * i));
    }
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> row((size_t)rowsize, 0);
    for (int y = 0; y < h; y++) {
        const uint8_t *src = rgb.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            row[(size_t)x * 3 + 0] = src[x * 3 + 2];
            row[(size_t)x * 3 + 1] = src[x * 3 + 1];
            row[(size_t)x * 3 + 2] = src[x * 3 + 0];
        }
        fwrite(row.data(), 1, (size_t)rowsize, f);
    }
    fclose(f);
    return true;
}

/* The capture path as a JSON string body (Windows paths carry backslashes). */
std::string jsonEscape(const std::string &text)
{
    std::string out;
    for (const char c : text) {
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

extern "C" void gfxParityCompare(const char *path, const uint8_t *glRgb, int w, int h,
                                 const uint8_t *bgfxRgba, int bw, int bh)
{
    const std::string base = path ? path : "parity";
    const std::string capture = jsonEscape(base);
    std::string json;
    char line[4096];

    if (w != bw || h != bh || !glRgb || !bgfxRgba) {
        sysLogPrintf(LOG_WARNING, "PARITY: %s size mismatch: GL %dx%d, bgfx %dx%d", base.c_str(), w, h, bw, bh);
        snprintf(line, sizeof(line),
                 "{\n  \"schema\": \"pd2.renderer-parity.v1\",\n  \"capture\": \"%s\",\n"
                 "  \"gl_size\": [%d, %d],\n  \"bgfx_size\": [%d, %d],\n  \"size_match\": false\n}\n",
                 capture.c_str(), w, h, bw, bh);
        json = line;
    } else {
        std::vector<uint8_t> bgfxRgb((size_t)w * h * 3);
        std::vector<uint8_t> diff((size_t)w * h * 3, 0);
        uint64_t differing = 0;
        int maxDelta = 0;
        uint64_t hist[4] = { 0, 0, 0, 0 }; /* max channel delta 1, 2-3, 4-15, 16+ */
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const size_t i = (size_t)y * w + x;
                const uint8_t *g = glRgb + i * 3;
                const uint8_t *b = bgfxRgba + i * 4;
                int d = 0;
                for (int c = 0; c < 3; c++) {
                    bgfxRgb[i * 3 + c] = b[c];
                    const int dc = g[c] > b[c] ? g[c] - b[c] : b[c] - g[c];
                    diff[i * 3 + c] = (uint8_t)(dc * 64 > 255 ? 255 : dc * 64);
                    if (dc > d) d = dc;
                }
                if (d > 0) {
                    differing++;
                    if (d > maxDelta) maxDelta = d;
                    hist[d == 1 ? 0 : (d <= 3 ? 1 : (d <= 15 ? 2 : 3))]++;
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
            }
        }
        writeBmp24(base + ".bgfx.bmp", w, h, bgfxRgb);
        writeBmp24(base + ".diff.bmp", w, h, diff);

        /* Bounding box in top-left image coordinates (rows were bottom-up). */
        const int top = y1 >= 0 ? h - 1 - y1 : -1;
        const int bottom = y0 < h ? h - 1 - y0 : -1;
        sysLogPrintf(LOG_NOTE,
            "PARITY: %s size=%dx%d differing=%llu maxdelta=%d hist(1,2-3,4-15,16+)=%llu,%llu,%llu,%llu bbox=%d,%d-%d,%d",
            base.c_str(), w, h, (unsigned long long)differing, maxDelta,
            (unsigned long long)hist[0], (unsigned long long)hist[1], (unsigned long long)hist[2],
            (unsigned long long)hist[3], differing ? x0 : -1, top, differing ? x1 : -1, bottom);
        snprintf(line, sizeof(line),
                 "{\n  \"schema\": \"pd2.renderer-parity.v1\",\n  \"capture\": \"%s\",\n"
                 "  \"gl_size\": [%d, %d],\n  \"bgfx_size\": [%d, %d],\n  \"size_match\": true,\n"
                 "  \"pixels\": %llu,\n  \"differing_pixels\": %llu,\n  \"max_channel_delta\": %d,\n"
                 "  \"delta_histogram\": {\"1\": %llu, \"2-3\": %llu, \"4-15\": %llu, \"16+\": %llu},\n"
                 "  \"bbox_top_left\": [%d, %d, %d, %d]\n}\n",
                 capture.c_str(), w, h, bw, bh, (unsigned long long)w * h, (unsigned long long)differing, maxDelta,
                 (unsigned long long)hist[0], (unsigned long long)hist[1], (unsigned long long)hist[2],
                 (unsigned long long)hist[3], differing ? x0 : -1, top, differing ? x1 : -1, bottom);
        json = line;
    }

    const std::string jsonPath = base + ".parity.json";
    FILE *f = fopen(jsonPath.c_str(), "wb");
    if (f) {
        fwrite(json.data(), 1, json.size(), f);
        fclose(f);
    } else {
        sysLogPrintf(LOG_ERROR, "PARITY: could not write %s", jsonPath.c_str());
    }
}
