#include "tga.h"
#include "util.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>

#pragma pack(push,1)
struct TGAHeader {
    uint8_t  idLength;
    uint8_t  colorMapType;
    uint8_t  imageType;
    uint16_t colorMapFirst;
    uint16_t colorMapLength;
    uint8_t  colorMapDepth;
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  imageDesc;
};
#pragma pack(pop)

bool loadTGA(const char* filename, COLOR3*& outData, int& width, int& height)
{
    FILE* f = fopen(filename, "rb");
    if (!f) return false;

    TGAHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) {
        logf("Unexpected eof loading TGA\n");
        fclose(f);
        return false;
    }

    // Only support uncompressed RGB/RGBA
    if ((h.imageType != 2 && h.imageType != 10) || (h.bpp != 24 && h.bpp != 32)) {
        logf("Unsupported TGA format\n");
        fclose(f);
        return false;
    }

    // Skip ID field if present
    if (h.idLength)
        fseek(f, h.idLength, SEEK_CUR);

    width = h.width;
    height = h.height;
    int channels = h.bpp / 8;

    size_t imageSize = width * height * channels;
    outData = new COLOR3[width * height];
    
    vector<uint8_t> pixels;
    pixels.resize(imageSize);

    uint8_t* dst = pixels.data();
    const size_t pixelCount = width * height;
    const int pixelSize = channels;

    if (h.imageType == 2) {
        // Uncompressed
        if (fread(dst, 1, imageSize, f) != imageSize) {
            logf("Unexpected eof loading TGA\n");
            fclose(f);
            return false;
        }
    }
    else {
        // RLE compressed (type 10)
        size_t pixelsRead = 0;

        while (pixelsRead < pixelCount) {
            uint8_t header;
            if (fread(&header, 1, 1, f) != 1) {
                logf("Unexpected eof loading TGA\n");
                fclose(f);
                return false;
            }

            int count = (header & 0x7F) + 1;

            if (header & 0x80) {
                // RLE packet
                uint8_t pixel[4];
                if (fread(pixel, 1, pixelSize, f) != pixelSize) {
                    logf("Unexpected eof loading TGA\n");
                    fclose(f);
                    return false;
                }

                for (int i = 0; i < count; i++) {
                    memcpy(dst, pixel, pixelSize);
                    dst += pixelSize;
                    pixelsRead++;
                }
            }
            else {
                // Raw packet
                size_t bytes = count * pixelSize;
                if (fread(dst, 1, bytes, f) != bytes) {
                    logf("Unexpected eof loading TGA\n");
                    fclose(f);
                    return false;
                }
                dst += bytes;
                pixelsRead += count;
            }
        }
    }

    fclose(f);

    // BGR(A) -> RGB(A)
    for (size_t i = 0; i < imageSize; i += channels) {
        std::swap(pixels[i + 0], pixels[i + 2]);
    }

    // Handle origin (bit 5: top-left if set)
    bool topOrigin = (h.imageDesc & 0x20) != 0;
    if (!topOrigin) {
        int rowSize = width * channels;
        std::vector<uint8_t> temp(rowSize);

        for (int y = 0; y < height / 2; y++) {
            uint8_t* rowA = &pixels[y * rowSize];
            uint8_t* rowB = &pixels[(height - 1 - y) * rowSize];

            memcpy(temp.data(), rowA, rowSize);
            memcpy(rowA, rowB, rowSize);
            memcpy(rowB, temp.data(), rowSize);
        }
    }

    memcpy(outData, &pixels[0], width * height * sizeof(COLOR3));

    return true;
}