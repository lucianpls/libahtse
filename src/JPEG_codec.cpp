/*
 * JPEG_Codec.cpp
 * C++ Wrapper around libjpeg, providing encoding and decoding functions
 *
 * use setjmp, the only safe way to mix C++ and libjpeg and still get error messages
 *
 * (C)Lucian Plesea 2016-2019
 */

#include "JPEG_codec.h"
#include <algorithm>
#include <string>

NS_AHTSE_START

// Look for the JPEG precision, also check a couple of major structural issues
static int get_precision(storage_manager &src)
{
    const unsigned char *buffer = reinterpret_cast<unsigned char *>(src.buffer);
    const unsigned char *sentinel = buffer + src.size;
    if (*buffer != 0xff || buffer[1] != 0xd8)
        return -1; // Error, SOI header not found
    buffer += 2;

    while (buffer < sentinel) {
        int sz;
        if (*buffer++ != 0xff)
            continue; // Skip non-markers, in case padding exists

        // Make sure we can read another byte
        if (buffer >= sentinel)
            return -1;

        // Flags with no size, RST, EOI, TEM and valid ff byte
        if (((*buffer & 0xf8) == 0xd0) || (*buffer == 0xd9) || (*buffer <= 1)) {
            buffer++;
            continue;
        }

        switch (*buffer++) {
        case 0xc0: // SOF0, baseline which includes the size and precision
        case 0xc1: // SOF1, also baseline

            // Precision is the byte right after the size
            if (buffer + 3 >= sentinel)
                return -1; // Error in JPEG
            sz = static_cast<int>(buffer[2]);
            if (sz != 8 && sz != 12) // Only 8 and 12 are valid values
                return -1;
            return sz; // Normal exit, found the precision

            // The precision is followed by y size and x size, each two bytes
            // in big endian order
            // Then comes 1 byte, number of components
            // Then 3 bytes per component
            // Byte 1, type
            //  1 - Y, 2 - Cb, 3 - Cr, 4 - I, 5 Q
            // Byte 2, sampling factors
            //  Bits 0-3 vertical, 4-7 horizontal
            // Byte 3, Which quantization table to use

        case 0xda:
            // Reaching the start of scan without finding the frame 0 is an error
            return -1;

        default: // Normal segments with size, safe to skip
            if (buffer + 2 >= sentinel)
                return -1;

            sz = (static_cast<int>(*buffer) << 8) | buffer[1];
            buffer += sz;
        }
    }
    return -1; // Something went wrong
}

// Dispatcher for 8 or 12 bit jpeg decoder
const char *jpeg_stride_decode(codec_params &params, storage_manager &src, void *buffer)
{
    int precision = get_precision(src);
    switch (precision) {
    case 8:
        return jpeg8_stride_decode(params, src, buffer);
    case 12:
        return jpeg12_stride_decode(params, src, buffer);
    }
    strcpy(params.error_message, "Input error, not recognized as JPEG");
    return params.error_message;
}

const char *jpeg_encode(jpeg_params &params, storage_manager &src, storage_manager &dst)
{
    const char* message = nullptr;
    switch (getTypeSize(params.dt)) {
    case 1:
        message = jpeg8_encode(params, src, dst);
        break;
    case 2:
        message = jpeg12_encode(params, src, dst);
        break;
    default:
        strcpy(params.error_message,
            "Usage error, only 8 and 12 bit input can be encoded as JPEG");
        message = params.error_message;
    }
    if (!message)
        return nullptr;
    // Had an error reported
    memcpy(params.error_message, message, std::min<size_t>(sizeof(params.error_message) - 1, strlen(message)));
    if (std::string::npos != std::string(message).find("Write to EMS")) {
        // Convert weird message to the actual reason
        strcpy(params.error_message, "Write buffer too small");
        message = params.error_message;
    }
    return message;
}

int set_jpeg_params(const TiledRaster& raster, codec_params* params) {
    memset(params, 0, sizeof(codec_params));
    params->size = raster.pagesize;
    params->dt = raster.datatype;
    return 0;
}

NS_AHTSE_END
