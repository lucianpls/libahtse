/*
* LERC_codec.cpp
* C++ Wrapper around lerc1, providing encoding and decoding functions
*
* (C)Lucian Plesea 2020
*/

#include "ahtse.h"
#include "Lerc1Image.h"

NS_AHTSE_USE

const char* lerc_encode(lerc_params& params, const TiledRaster& raster,
    storage_manager& src, storage_manager& dst)
{
    return nullptr;
}

const char* lerc_stride_decode(codec_params& params, const TiledRaster& raster,
    storage_manager& src, void* buffer)
{
    return nullptr;
}

int set_def_lerc_params(const TiledRaster& raster, lerc_params* params)
{
    return 0;
}
