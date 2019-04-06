/*
* ahtse.h
*
* Public interface to libahtse
*
* (C) Lucian Plesea 2019
*/

#if !defined(AHTSE_H)
#define AHTSE_H

#include <apr.h>

#define NS_AHTSE_START namespace AHTSE {
#define NS_AHTSE_END }
#define NS_AHTSE_USE using namespace AHTSE;

NS_AHTSE_START

#if defined(DEBUG) || defined(_DEBUG)
#define LOG(r, msg, ...) {\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, msg, ##__VA_ARGS__);\
}
#else
#define LOG()
#endif
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
#if APR_IS_BIGENDIAN // Big endian, do nothing

// These values are big endian
#define PNG_SIG 0x89504e47
#define JPEG_SIG 0xffd8ffe0

// Lerc is only supported on little endian
// #define LERC_SIG 0x436e745a

// This one is not an image type, but an encoding
#define GZIP_SIG 0x1f8b0800

#else // Little endian

// For formats that need net order
#define NEED_SWAP

#define PNG_SIG  0x474e5089
#define JPEG_SIG 0xe0ffd8ff
#define LERC_SIG 0x5a746e43

// This one is not an image type, but an encoding
#define GZIP_SIG 0x00088b1f

#endif

// Pixel value data types
// Copied and slightly modified from GDAL
DLL_PUBLIC typedef enum {
    /*! Unknown or unspecified type */ 		GDT_Unknown = 0,
    /*! Eight bit unsigned integer */           GDT_Byte = 1,
    GDT_Char = 1,
    /*! Sixteen bit unsigned integer */         GDT_UInt16 = 2,
    /*! Sixteen bit signed integer */           GDT_Int16 = 3,
    GDT_Short = 3,
    /*! Thirty two bit unsigned integer */      GDT_UInt32 = 4,
    /*! Thirty two bit signed integer */        GDT_Int32 = 5,
    GDT_Int = 5,
    /*! Thirty two bit floating point */        GDT_Float32 = 6,
    GDT_Float = 6,
    /*! Sixty four bit floating point */        GDT_Float64 = 7,
    GDT_Double = 7,
    GDT_TypeCount = 8          /* maximum type # + 1 */
} GDALDataType;

// Size in bytes
DLL_PUBLIC int GDTGetSize(GDALDataType dt, int num = 1);

// Return a GDAL data type by name
DLL_PUBLIC GDALDataType getDT(const char *name);

// Separate channels and level, just in case
struct sz {
    apr_int64_t x, y, z, c, l;
};

// Populates size and returns null if it works, error message otherwise
// "x y", "x y z" or "x y z c"
DLL_PUBLIC const char *get_xyzc_size(sz *size, const char *value);

struct bbox_t {
    double xmin, ymin, xmax, ymax;
};

struct storage_manager {
    char *buffer;
    int size;
};

struct empty_conf_t {
    // Empty tile in RAM, if defined
    storage_manager empty;
    // Buffer for the empty tile etag
    char eTag[16];
};

struct TiledRaster {
    // Size and pagesize of the raster
    struct sz size, pagesize;

    // Generic data values
    double ndv, min, max;
    int has_ndv, has_min, has_max;

    // how many levels from full size, computed
    int n_levels;
    // width and height for each pyramid level
    struct rset *rsets;
    // How many levels to skip at the top of the pyramid
    int skip;
    GDALDataType datatype;

    // geographical projection
    const char *projection;
    struct bbox_t bbox;

    // ETag initializer
    apr_uint64_t seed;
    // The Empty tile etag in string form, derived from seed
    empty_conf_t missing;
};

struct rset {
    // Resolution, units per pixel
    double rx, ry;
    // In tiles
    int w, h;
};

//
// Any decoder needs a static place for an error message and a line stride when decoding
// This structure is accepted by the decoders, regardless of type
// For encoders, see format specific extensions below
//

struct codec_params {
    // Line size in bytes
    apr_uint32_t line_stride;
    // Set if special data handling took place during decoding (zero mask on JPEG)
    apr_uint32_t modified;
    // A place for codec error message
    char error_message[1024];
};

struct jpeg_params : codec_params {
    int quality;
};

NS_AHTSE_END

#endif
