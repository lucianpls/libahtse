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

#if IS_BIGENDIAN // Big endian, do nothing

// These values are big endian
#define PNG_SIG 0x89504e47
#define JPEG_SIG 0xffd8ffe0

// Lerc is only supported on little endian
#define LERC_SIG 0x436e745a

// This one is not an image type, but an encoding
#define GZIP_SIG 0x1f8b0800

#else // Little endian

// For formats that need net order, equivalent to !IS_BIGENDIAN
#define NEED_SWAP

#if defined(_WIN32)
// Windows is always little endian, supply functions to swap bytes
 // These are defined in <cstdlib>
#define htobe16 _byteswap_ushort
#define be16toh _byteswap_ushort
#define htobe32 _byteswap_ulong
#define be32toh _byteswap_ulong
#define htobe64 _byteswap_uint64
#define be64toh _byteswap_uint64

#define le64toh(X) (X)
#define htole64(X) (X)

#else
// Assume linux
#include <endian.h>

#endif

#define PNG_SIG  0x474e5089
#define JPEG_SIG 0xe0ffd8ff
#define LERC_SIG 0x5a746e43

// This one is not an image type, but an encoding
#define GZIP_SIG 0x00088b1f

#endif

NS_AHTSE_START

// The maximum size of a tile
#define MAX_TILE_SIZE 4*1024*1024

// Accept empty tiles up to this size
#define MAX_READ_SIZE (1024*1024)

struct storage_manager {
    storage_manager(void) : buffer(nullptr), size(0) {}
    storage_manager(void* ptr, size_t sz) :
        buffer(reinterpret_cast<char*>(ptr)),
        size(static_cast<int>(sz)) {}
    char* buffer;
    int size;
};

// Empty tile runtime object
struct empty_conf_t {
    // Empty tile in RAM, if defined
    storage_manager data;
    // Buffer for the empty tile etag
    char eTag[16];
};

// Works as location also
#define sloc_t sz5

// Resolution set (level)
struct rset {
    // Resolution, units per pixel
    double rx, ry;
    // In tiles
    int w, h;
    // level starting offset, in tiles
    uint64_t tiles;
};

// Bounding box
struct bbox_t { double xmin, ymin, xmax, ymax; };

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