/*
* LERC_codec.cpp
* C++ Wrapper around lerc1, providing encoding and decoding functions
*
* Modified from: https://github.com/OSGeo/gdal/blob/master/gdal/frmts/mrf/LERC_band.cpp
* 
* (C)Lucian Plesea 2020
*/

#include "ahtse.h"
#include "Lerc1Image.h"

USING_NAMESPACE_LERC1
NS_AHTSE_START

// Read an unaligned 4 byte little endian int from location p, advances pointer
static void READ_INT32(uint32_t& X, const char*& p) {
    memcpy(&X, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
}

// Read an unaligned 4 byte little endian float from location p, advances pointer
static void READ_FLOAT(float& X, const char*& p) {
    memcpy(&X, p, sizeof(float));
    p += sizeof(float);
}

// Arbitrary epsilon, just like gdal CPLIsEqual()
static bool FIsEqual(float v1, float v2) {
    return abs(v1 - v2) < 1e-12;
}

template<typename T> static void Lerc1ImgFill(Lerc1Image& zImg, T* src, const lerc_params &params)
{
    int w = static_cast<int>(params.size.x);
    int h = static_cast<int>(params.size.y);
    zImg.resize(w, h);
    const float ndv = static_cast<float>(params.ndv);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            auto val = static_cast<float>(*src++);
            zImg(row, col) = val;
            zImg.mask.Set(row * w + col, !FIsEqual(ndv, val));
        }
}

const char* lerc_encode(lerc_params& params, storage_manager& src, storage_manager& dst)
{
    Lerc1Image zImg;
    auto pdst = reinterpret_cast<Lerc1NS::Byte*>(dst.buffer);

    switch (params.dt) {
#define FILL(T) Lerc1ImgFill(zImg, reinterpret_cast<T *>(src.buffer), params)
    case GDT_Byte: FILL(uint8_t); break;
    case GDT_UInt16: FILL(uint16_t); break;
    case GDT_Int16: FILL(int16_t); break;
    case GDT_UInt32: FILL(uint32_t); break;
    case GDT_Int32: FILL(int32_t); break;
    case GDT_Float32: FILL(float); break;
    default:
        return "Unsupported data type for LERC1 encode"; // Error return
    }
#undef FILL
    if (!zImg.write(&pdst, params.prec)) {
        return "Error during LERC1 compression";
    }
    return nullptr;
}

const char* lerc_stride_decode(codec_params& params, storage_manager& src, void* buffer)
{
    return "Not yet implemented";
}

int set_lerc_params(const TiledRaster& raster, lerc_params* params)
{
    memset(params, 0, sizeof(lerc_params));
    params->size = raster.pagesize;
    params->dt = raster.datatype;
    return 0;
}
NS_AHTSE_END
