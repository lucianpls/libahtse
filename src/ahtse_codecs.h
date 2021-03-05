/*
* ahtse_codecs.h
*
* Raster codecs only
* No include dependencies on apr or http
* No include dependencies on actual codec libraries
*
* (C) Lucian Plesea 2019-2021
*/

#pragma once

#if !defined(AHTSE_CODECS_H)
#define AHTSE_CODECS_H

#include <ahtse_common.h>

#if !defined(NS_AHTSE_START)
#define NS_AHTSE_START namespace AHTSE {
#define NS_AHTSE_END }
#define NS_AHTSE_USE using namespace AHTSE;
#endif

NS_AHTSE_START

// Pixel value data types
// Copied and slightly modified from GDAL
typedef enum {
    AHTSE_Unknown = 0,    // Unknown or unspecified type
    AHTSE_Byte = 1,       // Eight bit unsigned integer
    AHTSE_Char = 1,
    AHTSE_UInt16 = 2,     // Sixteen bit unsigned integer
    AHTSE_Int16 = 3,      // Sixteen bit signed integer
    AHTSE_Short = 3,
    AHTSE_UInt32 = 4,     // Thirty two bit unsigned integer
    AHTSE_Int32 = 5,      // Thirty two bit signed integer
    AHTSE_Int = 5,
    // Keep the floats at the end
    AHTSE_Float32 = 6,    // Thirty two bit floating point
    AHTSE_Float = 6,
    AHTSE_Float64 = 7,    // Sixty four bit floating point
    AHTSE_Double = 7
    //    AHTSE_TypeCount = 8   // Not a type
} AHTSEDataType;

// IMG_ANY is the default, but no checks can be done at config time
// On input, it decodes to byte, on output it is equivalent to IMG_JPEG
// JPEG is always JPEG_ZEN
enum IMG_T { IMG_ANY, IMG_JPEG, IMG_PNG, IMG_LERC, IMG_INVALID };

DLL_PUBLIC IMG_T getFMT(const std::string&);

// Size in bytes
DLL_PUBLIC int getTypeSize(AHTSEDataType dt, int num = 1);

// Return a data type by name
DLL_PUBLIC AHTSEDataType getDT(const char* name);

// Tile raster properties
struct TiledRaster {
    // Size and pagesize of the raster
    struct sz size, pagesize;

    // Generic data values
    double ndv, min, max, precision;
    int has_ndv, has_min, has_max;
    int maxtilesize;
    IMG_T format;

    // how many levels from full size, computed
    int n_levels;
    // width and height for each pyramid level
    rset* rsets;
    // How many levels to skip at the top of the pyramid
    int skip;
    AHTSEDataType datatype;

    // geographical projection
    const char* projection;
    struct bbox_t bbox;

    // ETag initializer
    uint64_t seed;
    // The Empty tile etag in string form, derived from seed
    empty_conf_t missing;
};

//
// Any decoder needs a static place for an error message and a line stride when decoding
// This structure is accepted by the decoders, regardless of type
// For encoders, see format specific extensions below
//
struct codec_params {
    codec_params() {
        memset(this, 0, sizeof(codec_params));
    }
    codec_params(const TiledRaster& raster) {
        memset(this, 0, sizeof(codec_params));
        size = raster.pagesize;
        dt = raster.datatype;
        if (raster.has_ndv)
            ndv = raster.ndv;
    }
    DLL_PUBLIC size_t min_buffer_size() const {
        return size.x * size.y * getTypeSize(dt);
    }
    sz size;
    AHTSEDataType dt; // data type
    IMG_T format;    // output from decode
    // Line size in bytes for decoding only
    uint32_t line_stride;
    // Set if special data handling took place during decoding (zero mask on JPEG)
    uint32_t modified;
    double ndv; // Defaults to zero, needed during decode for Lerc1
    // A place for codec error message
    char error_message[1024];
};

struct jpeg_params : codec_params {
    int quality;
};

struct png_params : codec_params {
    // As defined by PNG
    int color_type, bit_depth;
    // 0 to 9
    int compression_level;

    // If true, NDV is the transparent color
    int has_transparency;
    // TODO: Have a way to pass the transparent color when has_transparency is true
};

struct lerc_params : codec_params {
    float prec; // half of quantization step
};

// Generic image decode dispatcher, parameters should be already set to what is expected
// Returns error message or null.
DLL_PUBLIC const char* stride_decode(codec_params& params, storage_manager& src, void* buffer);

// In JPEG_codec.cpp
// raster defines the expected tile
// src contains the input JPEG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded JPEG line)
// Returns NULL if everything looks fine, or an error message
DLL_PUBLIC const char* jpeg_stride_decode(codec_params& params, storage_manager& src, void* buffer);
DLL_PUBLIC const char* jpeg_encode(jpeg_params& params, storage_manager& src, storage_manager& dst);
// Based on the raster configuration, populates a jpeg parameter structure, must call before encode and decode
DLL_PUBLIC int set_jpeg_params(const TiledRaster& raster, codec_params* params);

// In PNG_codec.cpp
// raster defines the expected tile
// src contains the input PNG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded PNG line)
// Returns NULL if everything looks fine, or an error message
DLL_PUBLIC const char* png_stride_decode(codec_params& params, storage_manager& src, void* buffer);
DLL_PUBLIC const char* png_encode(png_params& params, storage_manager& src, storage_manager& dst);
// Based on the raster configuration, populates a png parameter structure, must call before encode and decode
DLL_PUBLIC int set_png_params(const TiledRaster& raster, png_params* params);

// In LERC_codec.cpp
DLL_PUBLIC const char* lerc_stride_decode(codec_params& params, storage_manager& src, void* buffer);
DLL_PUBLIC const char* lerc_encode(lerc_params& params, storage_manager& src, storage_manager& dst);
// Based on the raster configuration, populates a png parameter structure
DLL_PUBLIC int set_lerc_params(const TiledRaster& raster, lerc_params* params);

NS_AHTSE_END
#endif