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
#include <httpd.h>
#include <http_config.h>
#include <cstdlib>
#include <cmath>

#define APR_WANT_STRFUNC
#define APR_WANT_MEMFUNC
#include <apr_want.h>
#include <apr_strings.h>

#if APR_SUCCESS != 0
#error "APR_SUCCESS is not null"
#endif

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
// because they are checked as a uint32_t
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

#if defined(_WIN32)
// Windows is always little endian, supply functions to swap bytes
 // These are defined in <cstdlib>
#define htobe16 _byteswap_ushort
#define be16toh _byteswap_ushort
#define htobe32 _byteswap_ulong
#define be32toh _byteswap_ulong
#define htobe64 _byteswap_uint64
#define be64toh _byteswap_uint64

#else
#include <endian.h>

#endif

#define PNG_SIG  0x474e5089
#define JPEG_SIG 0xe0ffd8ff
#define LERC_SIG 0x5a746e43

// This one is not an image type, but an encoding
#define GZIP_SIG 0x00088b1f

#endif

// The maximum size of a tile for MRF, to avoid MRF corruption errors
#define MAX_TILE_SIZE 4*1024*1024

// Pixel value data types
// Copied and slightly modified from GDAL
typedef enum {
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

enum img_fmt { IMG_JPEG, IMG_JPEG_ZEN, IMG_PNG };

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
    storage_manager(void) : buffer(nullptr), size(0) {}
    storage_manager(void *ptr, size_t sz) :
        buffer(reinterpret_cast<char *>(ptr)),
        size(static_cast<int>(sz)) {}
    char *buffer;
    int size;
};

struct empty_conf_t {
    // Empty tile in RAM, if defined
    storage_manager data;
    // Buffer for the empty tile etag
    char eTag[16];
};

struct rset {
    // Resolution, units per pixel
    double rx, ry;
    // In tiles
    int w, h;
    // level starting offset, in tiles
    apr_uint64_t tiles;
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
    rset *rsets;
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

// Generic range
struct range_t {
    apr_uint64_t offset;
    apr_uint64_t size;
};

// A virtual file name and a range of valid offsets
// May be local file or a redirect
// The range size will be ignored if set to zero
struct vfile_t {
    char *name;
    range_t range;
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

struct png_params : codec_params {
    // As defined by PNG
    int color_type, bit_depth;
    // 0 to 9
    int compression_level;

    // If true, NDV is the transparent color
    int has_transparency;
    // TODO: Have a way to pass the transparent color when has_transparency is on

};

#define READ_RIGHTS APR_FOPEN_READ | APR_FOPEN_BINARY | APR_FOPEN_LARGEFILE
// Accept empty tiles up to this size
#define MAX_READ_SIZE (1024*1024)

// removes and returns the value of the last element from an apr_array, as type
// will crash by dereferencing the null pointer if the array is empty
#define ARRAY_POP(arr, type) (*(type *)(apr_array_pop(arr)))

// Returns a bad request code if condition is met
#define RETURN_ERR_IF(X) if (X) { return HTTP_BAD_REQUEST;}

// Server error with log message
#define SERVER_ERR_IF(X, r, msg, ...) if (X) {\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, msg, ##__VA_ARGS__);\
    return HTTP_INTERNAL_SERVER_ERROR;\
}

// Add the compiled pattern tot the regexp array.  It allocates the array if necessary
DLL_PUBLIC const char *add_regexp_to_array(apr_pool_t *pool, 
    apr_array_header_t **parr, const char *pattern);

//
// Reads a text file and returns a table where the directive is the key
// and the rest of the line is the value.
// Empty lines and lines that start with # are not returned
//
DLL_PUBLIC apr_table_t *readAHTSEConfig(apr_pool_t *pool, 
    const char *fname, const char **err_message);

// Initialize a raster from a kvp table
DLL_PUBLIC const char *configRaster(apr_pool_t *pool, 
    apr_table_t *kvp, struct TiledRaster &raster);

// Reads a bounding box, x,y,X,Y order.  Expects up to four numbers in C locale, comma separated
DLL_PUBLIC const char *getBBox(const char *line, bbox_t &bbox);

// From a string in base32 returns a 64bit int plus the extra boolean flag
DLL_PUBLIC apr_uint64_t base32decode(const char *is, int *flag);

// Encode a 64 bit integer to base32 string. Buffer should be at least 13 chars
DLL_PUBLIC void tobase32(apr_uint64_t value, char *buffer, int flag = 0);

//
// Read the empty file in a provided storage buffer
// the buffer gets alocated from the pool
// returns error message if something went wrong
// Maximum read size is set by MAX_READ_SIZE macro
// The line can contain the size and offset, white space separated, before the file name
//
DLL_PUBLIC char *readFile(apr_pool_t *pool, storage_manager &empty, const char *line);

// Returns true if one of the regexps compiled in the array match the full request, 
// including args
DLL_PUBLIC bool requestMatches(request_rec *r, apr_array_header_t *arr);

// tokenize a string into an array, based on a character. Returns nullptr if unsuccessful
DLL_PUBLIC apr_array_header_t *tokenize(apr_pool_t *p, const char *src, char sep = '/');

// returns true if the If-None-Match request etag matches
DLL_PUBLIC int etagMatches(request_rec *r, const char *ETag);

// Returns an image and a 200 error code
// Sets the mime type if provided, but it doesn't overwrite an already set one
// Also sets gzip encoding if the content is gzipped and the requester handles it
// Does not handle conditional response or setting ETags, those should already be set
// src.buffer should hold at least 4 bytes
DLL_PUBLIC int sendImage(request_rec *r,
    const storage_manager &src, const char *mime_type = nullptr);

// Called with an empty tile configuration, send the empty tile with the proper ETag
// Handles conditional requests
DLL_PUBLIC int sendEmptyTile(request_rec *r, const empty_conf_t &empty);

// In JPEG_codec.cpp
// raster defines the expected tile
// src contains the input JPEG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded JPEG line)
// Returns NULL if everything looks fine, or an error message
DLL_PUBLIC const char *jpeg_stride_decode(codec_params &params, 
    const TiledRaster &raster, storage_manager &src, void *buffer);

DLL_PUBLIC const char *jpeg_encode(jpeg_params &params, 
    const TiledRaster &raster, storage_manager &src, storage_manager &dst);

// In PNG_codec.cpp
// raster defines the expected tile
// src contains the input PNG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded PNG line)
// Returns NULL if everything looks fine, or an error message
DLL_PUBLIC const char *png_stride_decode(codec_params &params,
    const TiledRaster &raster, storage_manager &src, void *buffer);
DLL_PUBLIC const char *png_encode(png_params &params,
    const TiledRaster &raster, storage_manager &src, storage_manager &dst);
// Based on the raster configuration, populates a png parameter structure
DLL_PUBLIC int set_def_png_params(const TiledRaster &raster, png_params *params);

// Skip the leading white spaces and return true for "On" or "1"
// otherwise it returns false
DLL_PUBLIC int getBool(const char *s);

// Fetch the request configuration if it exists, otherwise the per_directory one
template<typename T> T* get_conf(request_rec * const r, const module * const thism) {
    T *cfg = (T *)ap_get_module_config(r->request_config, thism);
    if (cfg) return cfg;
    return (T *)ap_get_module_config(r->per_dir_config, thism);
}

NS_AHTSE_END

#endif
