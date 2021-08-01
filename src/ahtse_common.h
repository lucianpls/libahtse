/*
* ahtse_common.h
* 
* not module and not codec related
* No dependencies on apr or httpd headers
* 
* (C) Lucian Plesea 2019-2021
*/
#pragma once

#if !defined(AHTSE_COMMON_H)
#define AHTSE_COMMON_H

#include <cstdint>
#include <cstring>
#include <string>
#include <icd_codecs.h>

#define NS_AHTSE_START namespace AHTSE {
#define NS_AHTSE_END }
#define NS_AHTSE_USE using namespace AHTSE;

//
// Define DLL_PUBLIC to make a symbol visible
// Define DLL_LOCAL to hide a symbol
// Default behavior is system depenent
//

#ifdef DLL_PUBLIC
#undef DLL_PUBLIC
#endif

#ifdef DLL_LOCAL
#undef DLL_LOCAL
#endif

#if defined _WIN32 || defined __CYGWIN__
#define DLL_LOCAL

#ifdef LIBAHTSE_EXPORTS

#ifdef __GNUC__
#define DLL_PUBLIC __attribute__ ((dllexport))
#else
#define DLL_PUBLIC __declspec(dllexport)
#endif
#else
#ifdef __GNUC__
#define DLL_PUBLIC __attribute__ ((dllimport))
#else
#define DLL_PUBLIC __declspec(dllimport)
#endif
#endif

#else

#if __GNUC__ >= 4
#define DLL_PUBLIC __attribute__ ((visibility ("default")))
#define DLL_LOCAL  __attribute__ ((visibility ("hidden")))
#else
#define DLL_PUBLIC
#define DLL_LOCAL
#endif

#endif

// Conversion to and from network order, endianess depenent
// Define 4cc signatures for known types, with the correct endianess
// because they are checked as a uint32_t

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define IS_BIGENDIAN
#else
#endif

NS_AHTSE_START

// The maximum size of a tile
#define MAX_TILE_SIZE 4*1024*1024

// Accept empty tiles up to this size
#define MAX_READ_SIZE (1024*1024)

// Empty tile runtime object
struct empty_conf_t {
    // Empty tile in RAM, if defined
    ICD::storage_manager data;
    // Buffer for the empty tile etag
    char eTag[16];
};

// Works as location also
#define sloc_t ICD::sz5

// Resolution set (level)
struct rset {
    // Resolution, units per pixel
    double rx, ry;
    // In tiles
    size_t w, h;
    // level starting offset, in tiles
    uint64_t tiles;
};

// Bounding box
struct bbox_t { double xmin, ymin, xmax, ymax; };

// Tile and pyramid raster, with some metadata
// Does not contain C++ objects
struct TiledRaster : public ICD::Raster {
    ICD::sz5 pagesize;
    size_t maxtilesize; // In bytes

    size_t n_levels;
    size_t skip; // For ahtse addressing
    rset* rsets;

    // Geo information
    const char *projection;
    bbox_t bbox;

    // HTTP
    uint64_t seed;
    empty_conf_t missing;

    // Potentially format specific metadata
    // LERC
    double precision;
    size_t pagebytes() const {
        return getTypeSize(dt) * pagesize.x * pagesize.y * pagesize.c;
    }
};

// From a string in base32 returns a 64 + 1 bit integer
// The b65 is the lowest bit of first character, as if it would be in position 60
DLL_PUBLIC uint64_t base32decode(const char* is, int* b65);

// Encode a 64 + 1 bit integer to base32 string. Buffer should be at least 13 chars
// This is the reverse of the function declared above
DLL_PUBLIC void tobase32(uint64_t value, char* buffer, int b65 = 0);

// Reads a bounding box, x,y,X,Y order.  Expects up to four numbers in C locale, comma separated
DLL_PUBLIC const char* getBBox(const char* line, bbox_t& bbox);

// Skip the leading white spaces and return true for "On" or "1"
// otherwise it returns false
DLL_PUBLIC int getBool(const char* s);

// Populates size and returns null if it works, error message otherwise
// "x y", "x y z" or "x y z c"
DLL_PUBLIC const char* get_xyzc_size(ICD::sz5* size, const char* value);

NS_AHTSE_END
#endif