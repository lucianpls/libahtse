/*
* LERC_codec.cpp
* C++ Wrapper around lerc1, providing encoding and decoding functions
*
* Modified from: https://github.com/OSGeo/gdal/blob/master/gdal/frmts/mrf/LERC_band.cpp
* 
* (C)Lucian Plesea 2020
* Lerc1 is only little endian, it should not be compiled on big endian machines
*/

#include "ahtse.h"
static_assert(!APR_IS_BIGENDIAN, "Lerc 1 only works on little endian CPUs");

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

template<typename T> static void Lerc1ImgFill(Lerc1Image& zImg, T* src, const lerc_params &params) {
    int w = static_cast<int>(params.size.x);
    int h = static_cast<int>(params.size.y);
    zImg.resize(w, h);
    const float ndv = static_cast<float>(params.ndv);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            auto val = static_cast<float>(*src++);
            zImg(row, col) = val;
            zImg.SetMask(row, col, !FIsEqual(ndv, val));
        }
}

const char* lerc_encode(lerc_params& params, storage_manager& src, storage_manager& dst) {
    Lerc1Image zImg;

    switch (params.dt) {
#define FILL(T) Lerc1ImgFill(zImg, reinterpret_cast<T *>(src.buffer), params)
    case AHTSE_Byte: FILL(uint8_t); break;
    case AHTSE_UInt16: FILL(uint16_t); break;
    case AHTSE_Int16: FILL(int16_t); break;
    case AHTSE_UInt32: FILL(uint32_t); break;
    case AHTSE_Int32: FILL(int32_t); break;
    case AHTSE_Float32: FILL(float); break;
    default:
        return "Unsupported data type for LERC1 encode"; // Error return
    }
#undef FILL
    auto buffer = reinterpret_cast<Lerc1NS::Byte*>(dst.buffer);
    auto pdst = buffer;
    if (!zImg.write(&pdst, params.prec))
        return "Error during LERC1 compression";
    // Write advances the pdst pointer
    if (pdst - buffer > dst.size)
        // This is a late check, better than never?
        return "Output buffer overflow";
    dst.size = static_cast<int>(pdst - buffer);
    return nullptr;
}

// Check that this tile content looks like a LERC1 format array
static int checkV1(const char* s, size_t sz) {
    if (sz < 67) // Minimal LERC1, all no data compressed size
        return false;
    std::string l1sig(s, s + 10);
    if (l1sig != "CntZImage ")
        return false;
    s += 10;
    uint32_t version, tpe;
    READ_INT32(version, s); READ_INT32(tpe, s);
    if (version != 11 || tpe != 8)
        return false;
    uint32_t h, w;
    READ_INT32(h, s); READ_INT32(w, s);
    if (w > 20000 || h > 20000) return false;
    // skip the maxVal double
    s += sizeof(double);

    // First array header is the mask, 0 blocks
    READ_INT32(h, s); READ_INT32(w, s);
    if (h > 0 || w > 0) return false;

    uint32_t msz; // Mask size, in bytes
    READ_INT32(msz, s);
    // mask max value shold be 1, or maybe 0 (empty)
    float mmval;
    READ_FLOAT(mmval, s);
    if (mmval != 0.0f && mmval != 1.0f) return false;

    // mask bytes, plus header up to this point, plus data header
    if (msz + 50 + 16 > sz)
        return false;
    s += msz;

    // Second array, data
    // Data block count, never single pixels
    READ_INT32(h, s);
    READ_INT32(w, s);
    if (h > 10000 || w > 10000) return false;
    uint32_t dsz; // data size, in bytes
    READ_INT32(dsz, s);
    // Good enough
    return (static_cast<size_t>(50 )+ 16 + msz + dsz <= sz);
}

template <typename T> static void Lerc1ImgUFill(Lerc1Image& zImg, 
    const codec_params &params, T* buffer) {
    const T ndv = static_cast<T>(params.ndv);
    for (int row = 0; row < zImg.getHeight(); row++) {
        auto ptr = reinterpret_cast<T*>(reinterpret_cast<char *>(buffer) 
            + static_cast<size_t>(row) * params.line_stride);
        for (int col = 0; col < zImg.getWidth(); col++)
            *ptr++ = zImg.IsValid(row, col) ? static_cast<T>(zImg(row, col)) : ndv;
    }
}

const char* lerc_stride_decode(codec_params& params, storage_manager& src, void* buffer) {
    if (params.size.c != 1)
        return "Lerc1 multi-band is not supported";
    if (!checkV1(src.buffer, src.size))
        return "Not a Lerc1 tile";

    // Set default line stride if it wasn't specified explicitly
    if (0 == params.line_stride)
        params.line_stride = static_cast<apr_uint32_t>(params.size.x * getTypeSize(params.dt));
    Lerc1Image zImg;

    size_t nRemainingBytes = src.size;
    auto ptr = reinterpret_cast<Lerc1NS::Byte*>(src.buffer);
    if (!zImg.read(&ptr, nRemainingBytes, 1e12))
        return "Error during LERC decompression";
    if (zImg.getHeight() != params.size.y || zImg.getWidth() != params.size.x)
        return "Image received has the wrong size";

    // Got the data and the mask in zImg
#define UFILL(T) Lerc1ImgUFill(zImg, params, reinterpret_cast<T*>(buffer))
    switch (params.dt) {
    case AHTSE_Byte: UFILL(uint8_t); break;
    case AHTSE_UInt16: UFILL(uint16_t); break;
    case AHTSE_Int16: UFILL(int16_t); break;
    case AHTSE_UInt32: UFILL(uint32_t); break;
    case AHTSE_Int32: UFILL(int32_t); break;
    case AHTSE_Float: UFILL(float); break;
    default: break;
    }
#undef UFILL

    return nullptr; // Success
}

int set_lerc_params(const TiledRaster& raster, lerc_params* params) {
    memset(params, 0, sizeof(lerc_params));
    params->size = raster.pagesize;
    params->dt = raster.datatype;
    params->prec = static_cast<float>(raster.precision);
    return 0;
}
NS_AHTSE_END
