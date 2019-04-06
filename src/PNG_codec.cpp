/*
* PNG_codec.cpp
* C++ Wrapper around libpng, providing encoding and decoding functions
*
* This code only handles a basic subset of the PNG capabilities
*
* (C)Lucian Plesea 2016-2018
*/

// #include "mod_convert.h"
// #include <png.h>
#include "ahtse_util.h"
#include <vector>
#include <png.h>

// TODO: Add palette PNG support, possibly other fancy options

// Memory output doesn't need flushing
static void flush_png(png_structp) {};

// Do nothing for warnings
static void pngWH(png_struct *png, png_const_charp message) {};

// Error function
static void pngEH(png_struct *png, png_const_charp message)
{
    throw message;
}

// Read memory handler for PNG
static void get_data(png_structp pngp, png_bytep data, png_size_t length)
{
    storage_manager *src = static_cast<storage_manager *>(png_get_io_ptr(pngp));
    if (static_cast<png_size_t>(src->size) < length)
        throw "PNG decode expects more data than given";
    memcpy(data, src->buffer, length);
    src->buffer += length;
    src->size -= length;
}

// Write memory handler for PNG
static void store_data(png_structp pngp, png_bytep data, png_size_t length)
{
    storage_manager *dst = static_cast<storage_manager *>(png_get_io_ptr(pngp));
    if (static_cast<png_size_t>(dst->size) < length)
        throw static_cast<png_const_charp>("PNG encode buffer overflow");
    memcpy(dst->buffer, data, length);
    dst->buffer += length;
    dst->size -= length;
}

const char *png_stride_decode(codec_params &params,
    const TiledRaster &raster,
    storage_manager &src,
    void *buffer)
{
    char *message = NULL;
    png_structp pngp = NULL;
    png_infop infop = NULL;
    std::vector<png_bytep> png_rowp(static_cast<int>(raster.pagesize.y));
    for (size_t i = 0; i < png_rowp.size(); i++) // line_stride is always in bytes
        png_rowp[i] = reinterpret_cast<png_bytep>(
            static_cast<char *>(buffer)+i * params.line_stride);

    try {
        png_uint_32 width, height;
        int bit_depth, ct;
        pngp = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, pngEH, pngEH);
        infop = png_create_info_struct(pngp);
        png_set_read_fn(pngp, &src, get_data);

        // This reads all chunks up to the first IDAT
        png_read_info(pngp, infop);
        png_get_IHDR(pngp, infop, &width, &height, &bit_depth, &ct, NULL, NULL, NULL);

        if (static_cast<png_uint_32>(raster.pagesize.y) != height 
            || static_cast<png_uint_32>(raster.pagesize.x) != width)
            throw "Input PNG has the wrong size";

        if (png_get_rowbytes(pngp, infop) > params.line_stride)
            throw "Wrong type of data in PNG decode";

#if defined(NEED_SWAP)
        if (bit_depth > 8)
            png_set_swap(pngp);
#endif

        // TODO: Decode to expected format
        //        png_set_palette_to_rgb(pngp); // Palette to RGB
        //        png_set_tRNS_to_alpha(pngp);  // transparency palette to Alpha
        //        png_set_add_alpha(pngp, 255, PNG_FILLER_AFTER); // Add alpha if not there
        //        png_read_update_info(pngp, infop); // update the reader

        // Call this after using any of the png_set_*
        png_read_update_info(pngp, infop);

        png_read_image(pngp, png_rowp.data());
        png_read_end(pngp, infop);
    }
    catch (const char *error) {
        message = params.error_message;
        strcpy(message, error);
    }
    catch (...) { // Just in case
        message = params.error_message;
        strcpy(message, "Unknown PNG decode error");
    }

    png_destroy_read_struct(&pngp, &infop, 0);

    return message;
}

const char *png_encode(png_params &params, const TiledRaster &raster, 
    storage_manager &src, storage_manager &dst)
{
    char *message = NULL;
    png_structp pngp = NULL;
    png_infop infop = NULL;
    png_uint_32 width = static_cast<png_uint_32>(raster.pagesize.x);
    png_uint_32 height = static_cast<png_uint_32>(raster.pagesize.y);
    // Use a vector so it cleans up itself
    std::vector<png_bytep> png_rowp(height);

    // To avoid changing the buffer pointer
    storage_manager mgr = dst;

    try {
        pngp = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, pngEH, pngWH);
        infop = png_create_info_struct(pngp);
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

        png_write_info(pngp, infop);

#if defined(NEED_SWAP)
        if (params.bit_depth > 8)
            png_set_swap(pngp);
#endif

        int rowbytes = png_get_rowbytes(pngp, infop);
        for (size_t i = 0; i < png_rowp.size(); i++)
            png_rowp[i] = reinterpret_cast<png_bytep>(src.buffer + i * rowbytes);

        png_write_image(pngp, png_rowp.data());
        png_write_end(pngp, infop);
    }
    catch (const char *error) {
        message = params.error_message;
        strcpy(message, error);
    }
    catch (...) { // Just in case
        message = params.error_message;
        strcpy(message, "Unknown PNG encode error");
    }

    png_destroy_write_struct(&pngp, &infop);
    dst.size -= mgr.size; // mgr.size is bytes left

    return message;
}

int set_def_png_params(const TiledRaster &raster, png_params *params) {
    // Pick some defaults
    // Only handles 8 or 16 bits
    memset(params, 0, sizeof(png_params));
    params->bit_depth = raster.datatype == GDT_Byte ? 8 : 16;
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
