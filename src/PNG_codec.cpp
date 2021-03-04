/*
* PNG_codec.cpp
* C++ Wrapper around libpng, providing encoding and decoding functions
*
* This code only handles a basic subset of the PNG capabilities
*
* (C)Lucian Plesea 2016-2020
*/

#include "ahtse.h"
#include <vector>
#include <png.h>

NS_AHTSE_START

// TODO: Add palette PNG support, possibly other fancy options

// Memory output doesn't need flushing
static void flush_png(png_structp) {};

// Do nothing for warnings
static void pngWH(png_structp pngp, png_const_charp message) {};

// Error function
static void pngEH(png_structp pngp, png_const_charp message)
{
    codec_params* params = reinterpret_cast<codec_params*>(png_get_error_ptr(pngp));
    strncpy(params->error_message, message, 1024);
    longjmp(png_jmpbuf(pngp), 1);
}

// Read memory handler for PNG
static void get_data(png_structp pngp, png_bytep data, png_size_t length)
{
    storage_manager *src = static_cast<storage_manager *>(png_get_io_ptr(pngp));
    if (static_cast<png_size_t>(src->size) < length) {
        codec_params* params = (codec_params*)(png_get_error_ptr(pngp));
        strcpy(params->error_message, "PNG decode expects more data than given");
        longjmp(png_jmpbuf(pngp), 1);
    }
    memcpy(data, src->buffer, length);
    src->buffer += length;
    src->size -= static_cast<int>(length);
}

// Write memory handler for PNG
static void store_data(png_structp pngp, png_bytep data, png_size_t length)
{
    storage_manager *dst = static_cast<storage_manager *>(png_get_io_ptr(pngp));
    if (static_cast<png_size_t>(dst->size) < length) {
        codec_params* params = (codec_params*)(png_get_error_ptr(pngp));
        strcpy(params->error_message, "PNG encode buffer overflow");
        longjmp(png_jmpbuf(pngp), 1);
    }
    memcpy(dst->buffer, data, length);
    dst->buffer += length;
    dst->size -= static_cast<int>(length);
}

const char *png_stride_decode(codec_params &params, storage_manager &src, void *buffer)
{
    png_structp pngp = nullptr;
    png_infop infop = nullptr;
    png_uint_32 width, height;
    int bit_depth, ct;
    pngp = png_create_read_struct(PNG_LIBPNG_VER_STRING, &params, pngEH, pngEH);
    if (!pngp)
        return "PNG error while creating decode PNG structure";
    infop = png_create_info_struct(pngp);
    if (!infop)
        return "PNG error while creating decode info structure";

    if (setjmp(png_jmpbuf(pngp)))
        return params.error_message;

    png_set_read_fn(pngp, &src, get_data);

    // This reads all chunks up to the first IDAT
    png_read_info(pngp, infop);

    png_get_IHDR(pngp, infop, &width, &height, &bit_depth, &ct, NULL, NULL, NULL);

    if (static_cast<png_uint_32>(params.size.y) != height
        || static_cast<png_uint_32>(params.size.x) != width) {
        strcpy(params.error_message, "Input PNG has the wrong size");
        longjmp(png_jmpbuf(pngp), 1);
    }

    if ((params.dt == AHTSE_Byte && bit_depth != 8) ||
        ((params.dt == AHTSE_UInt16 || params.dt == AHTSE_Int16) && bit_depth != 16)) {
        strcpy(params.error_message, "Input PNG has the wrong type");
        longjmp(png_jmpbuf(pngp), 1);
    }

#if defined(NEED_SWAP)
    if (bit_depth > 8)
        png_set_swap(pngp);
#endif

    // TODO: Decode to expected format
    // png_set_palette_to_rgb(pngp); // Palette to RGB
    // png_set_tRNS_to_alpha(pngp); // transparency to Alpha
    // png_set_add_alpha(pngp, 255, PNG_FILTER_AFTER); // Add alpha if not there
    // Call this after using any of the png_set_*
    png_read_update_info(pngp, infop);

    auto line_stride = static_cast<png_size_t>(params.line_stride);
    if (0 == line_stride)
        line_stride = png_get_rowbytes(pngp, infop);

    std::vector<png_bytep> png_rowp(static_cast<int>(params.size.y));
    for (size_t i = 0; i < png_rowp.size(); i++) // line_stride is always in bytes
        png_rowp[i] = reinterpret_cast<png_bytep>(
            static_cast<char*>(buffer) + i * line_stride);

    png_read_image(pngp, png_rowp.data());
    png_read_end(pngp, infop);
    png_destroy_read_struct(&pngp, &infop, 0);

    return nullptr;
}

const char *png_encode(png_params &params, storage_manager &src, storage_manager &dst)
{
    png_structp pngp = nullptr;
    png_infop infop = nullptr;
    png_uint_32 width = static_cast<png_uint_32>(params.size.x);
    png_uint_32 height = static_cast<png_uint_32>(params.size.y);
    // Use a vector so it cleans up itself
    std::vector<png_bytep> png_rowp(height);

    // To avoid changing the buffer pointer
    storage_manager mgr = dst;

    pngp = png_create_write_struct(PNG_LIBPNG_VER_STRING, &params, pngEH, pngWH);
    if (!pngp)
        return "PNG error while creating encoding PNG structure";
    infop = png_create_info_struct(pngp);
    if (!infop)
        return "PNG error while creating encoding info structure";
    if (setjmp(png_jmpbuf(pngp)))
        return params.error_message;

    png_set_write_fn(pngp, &mgr, store_data, flush_png);
    png_set_IHDR(pngp, infop, width, height, params.bit_depth, params.color_type,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_set_compression_level(pngp, params.compression_level);

    // Flag NDV as transparent color
    if (params.has_transparency) {
        // TODO: Pass the transparent color via params.
        // For now, 0 is the no data value, regardless of the type of data

        png_color_16 tcolor;
        memset(&tcolor, 0, sizeof(png_color_16));
        png_set_tRNS(pngp, infop, 0, 0, &tcolor);
    }

#if defined(NEED_SWAP)
    if (params.bit_depth > 8)
        png_set_swap(pngp);
#endif

    auto rowbytes = png_get_rowbytes(pngp, infop);
    for (size_t i = 0; i < png_rowp.size(); i++)
        png_rowp[i] = reinterpret_cast<png_bytep>(src.buffer + i * rowbytes);

    png_write_info(pngp, infop);
    png_write_image(pngp, png_rowp.data());
    png_write_end(pngp, infop);

    png_destroy_write_struct(&pngp, &infop);
    dst.size -= mgr.size; // mgr.size is bytes left

    return nullptr;
}

int set_png_params(const TiledRaster &raster, png_params *params) {
    // Pick some defaults
    // Only handles 8 or 16 bits
    memset(params, 0, sizeof(png_params));
    params->size = raster.pagesize;
    params->dt = raster.datatype;
    params->bit_depth = (params->dt == AHTSE_Byte) ? 8 : 16;
    params->compression_level = 6;
    params->has_transparency = FALSE;

    switch (raster.pagesize.c) {
    case 1:
        params->color_type = PNG_COLOR_TYPE_GRAY;
        break;
    case 2:
        params->color_type = PNG_COLOR_TYPE_GA;
        break;
    case 3:
        params->color_type = PNG_COLOR_TYPE_RGB;
        break;
    case 4:
        params->color_type = PNG_COLOR_TYPE_RGBA;
        break;
    }
    return 0;
}

NS_AHTSE_END
