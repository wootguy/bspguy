#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <stdint.h>

bool lzmaCompress(std::string inPath, std::string outPath, uint32_t preset);

bool lzmaDecompress(uint8_t* compressedData, int compressedDataLen, std::vector<uint8_t>& outBytes);