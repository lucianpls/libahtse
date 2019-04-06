/*
* ahtse_util.h
*
* Common parts of AHTSE
*
* (C) Lucian Plesea 2018
*/

#if !defined(AHTSE_UTIL_H)
#define AHTSE_UTIL_H
#include "ahtse.h"
#include <httpd.h>
#include <apr.h>
#include <apr_tables.h>
#include <apr_pools.h>

// Always include httpd.h before other http* headers
// #include <httpd.h>
// #include <http_config.h>

// Byte swapping, linux style
#if defined(WIN32) // Windows
//#define __builtin_bswap16(v) _byteswap_ushort(v)
//#define __builtin_bswap32(v) _byteswap_ulong(v)
//#define __builtin_bswap64(v) _byteswap_uint64(v)
#endif

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

#if defined(DEBUG)
#define LOG(r, msg, ...) {\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, msg, ##__VA_ARGS__);\
}
#else
#define LOG()
#endif

// Separate channels and level, just in case
struct sz {
    apr_int64_t x, y, z, c, l;
};

struct bbox_t {
    double xmin, ymin, xmax, ymax;
};

typedef struct {
    char *buffer;
    int size;
} storage_manager;

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

// Return a GDAL data type by name
GDALDataType getDT(const char *name);

// Populates size and returns null if it works, error message otherwise
// "x y", "x y z" or "x y z c"
const char *get_xyzc_size(struct sz *size, const char *s);

// Add the compiled pattern tot the regexp array.  It allocates the array if necessary
const char *add_regexp_to_array(apr_pool_t *pool, apr_array_header_t **parr, const char *pattern);

//
// Reads a text file and returns a table where the first token of each line is the key
// and the rest of the line is the value.  Empty lines and lines that start with # are ignored
//
apr_table_t *readAHTSEConfig(apr_pool_t *pool, const char *fname, const char **err_message);

// Initialize a raster from a kvp table
const char *configRaster(apr_pool_t *pool, apr_table_t *kvp, struct TiledRaster &raster);

// Return a GDALDataType from a name, or GDT_Byte
GDALDataType getDT(const char *name);

// Reads a bounding box, x,y,X,Y order.  Expects up to four numbers in C locale, comma separated
const char *getBBox(const char *line, bbox_t &bbox);

// From a string in base32 returns a 64bit int plus the extra boolean flag
apr_uint64_t base32decode(const char *is, int *flag);

// Encode a 64 bit integer to base32 string. Buffer should be at least 13 chars
void tobase32(apr_uint64_t value, char *buffer, int flag = 0);

//
// Read the empty file in a provided storage buffer
// the buffer gets alocated from the pool
// returns error message if something went wrong
// Maximum read size is set by MAX_READ_SIZE macro
// The line can contain the size and offset, white space separated, before the file name
//
char *readFile(apr_pool_t *pool, storage_manager &empty, const char *line);

// Returns true if one of the regexps compiled in the array match the full request, including args
bool requestMatches(request_rec *r, apr_array_header_t *arr);

// tokenize a string into an array, based on a character. Returns nullptr if unsuccessful
apr_array_header_t *tokenize(apr_pool_t *p, const char *src, char sep = '/');

// returns true if the If-None-Match request etag matches
int etagMatches(request_rec *r, const char *ETag);

// Returns an image and a 200 error code
// Sets the mime type if provided, but it doesn't overwrite an already set one
// Also sets gzip encoding if the content is gzipped and the requester handles it
// Does not handle conditional response or setting ETags, those should already be set
// src.buffer should hold at least 4 bytes
int sendImage(request_rec *r, const storage_manager &src, const char *mime_type = nullptr);

// Called with an empty tile configuration, send the empty tile with the proper ETag
// Handles conditional requests
int sendEmptyTile(request_rec *r, const empty_conf_t &empty);

enum img_fmt { IMG_JPEG, IMG_JPEG_ZEN, IMG_PNG };

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

// In JPEG_codec.cpp
// raster defines the expected tile
// src contains the input JPEG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded JPEG line)
// Returns NULL if everything looks fine, or an error message
struct jpeg_params : codec_params {
    int quality;
};

const char *jpeg_stride_decode(codec_params &params, const TiledRaster &raster, storage_manager &src,
    void *buffer);
const char *jpeg_encode(jpeg_params &params, const TiledRaster &raster, storage_manager &src,
    storage_manager &dst);

struct png_params : codec_params {
    // As defined by PNG
    int color_type, bit_depth;
    // 0 to 9
    int compression_level;

    // If true, NDV is the transparent color
    int has_transparency;
    // TODO: Have a way to pass the transparent color when has_transparency is on

};

// In PNG_codec.cpp
// raster defines the expected tile
// src contains the input PNG
// buffer is the location of the first byte on the first line of decoded data
// line_stride is the size of a line in buffer (larger or equal to decoded PNG line)
// Returns NULL if everything looks fine, or an error message
const char *png_stride_decode(codec_params &params, const TiledRaster &raster,
    storage_manager &src, void *buffer);
const char *png_encode(png_params &params, const TiledRaster &raster,
    storage_manager &src, storage_manager &dst);
// Based on the raster configuration, populates a png parameter structure
int set_def_png_params(const TiledRaster &raster, png_params *params);

// Skip the leading white spaces and return true for "On" or "1"
// otherwise it returns false
int get_bool(const char *s);

#endif
