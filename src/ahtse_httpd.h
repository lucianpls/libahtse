/*
* ahtse_httpd.h
*
* Functionality dependent on apr or httpd
*
* (C) Lucian Plesea 2019-2021
*/
#pragma once

#if !defined(AHTSE_HTTPD_H)
#define AHTSE_HTTPD_H
#include <ahtse_common.h>
#include <apr.h>
#include <httpd.h>
#include <http_config.h>

#define APR_WANT_STRFUNC
#define APR_WANT_MEMFUNC
#include <apr_want.h>
#include <apr_strings.h>
#include <apr_hash.h>

#if APR_SUCCESS != 0
#error "APR_SUCCESS is not zero"
#endif

NS_AHTSE_START

#if defined(NEED_SWAP) && (defined(APR_IS_BIGENDIAN) && (APR_IS_BIGENDIAN != 0))
#error APR endianess mismatch
#endif

// Logging functions that vanish in release builds
#if defined(DEBUG) || defined(_DEBUG)
#define LOG(r, msg, ...) {\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, msg, ##__VA_ARGS__);\
}
#define LOGNOTE(r, msg, ...) {\
    ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, msg, ##__VA_ARGS__);\
}
#else
#define LOG(...)
#define LOGNOTE(...)
#endif

// Generic range
struct range_t {
    apr_uint64_t offset;
    apr_uint64_t size;
};

// A virtual file name and a range of valid offsets
// May be local file or a redirect
// The range size will be ignored if set to zero
struct vfile_t {
    char* name;
    range_t range;
};

#define READ_RIGHTS APR_FOPEN_READ | APR_FOPEN_BINARY | APR_FOPEN_LARGEFILE

// removes and returns the value of the last element from an apr_array, as type
// will crash by dereferencing the null pointer if the array is empty
#define ARRAY_POP(arr, type) (*(type *)(apr_array_pop(arr)))

// Returns a bad request code if condition is met
#define RETURN_ERR_IF(X) if (X) { return HTTP_BAD_REQUEST;}

// Server error with log message
// It is a macro because the APLOG_MARK depends on the module used
#define SERVER_ERR_IF(X, r, msg, ...) if (X) {\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, msg, ##__VA_ARGS__);\
    return HTTP_INTERNAL_SERVER_ERROR;\
}

// Add the compiled pattern tot the regexp array.  It allocates the array if necessary
DLL_PUBLIC const char* add_regexp_to_array(apr_pool_t* pool,
    apr_array_header_t** parr, const char* pattern);

//
// Reads a text file and returns a table where the directive is the key
// and the rest of the line is the value.
// Empty lines and lines that start with # are not returned
//
DLL_PUBLIC apr_table_t* readAHTSEConfig(apr_pool_t* pool,
    const char* fname, const char** err_message);

// Initialize a raster structure from a temporary kvp table
DLL_PUBLIC const char* configRaster(apr_pool_t* pool,
    apr_table_t* kvp, TiledRaster& raster);

//
// Read the empty file in a provided storage buffer
// the buffer gets alocated from the pool
// returns error message if something went wrong
// Maximum read size is set by MAX_READ_SIZE macro
// The line can contain the size and offset, white space separated, before the file name
//
DLL_PUBLIC char* readFile(apr_pool_t* pool, ICD::storage_manager& empty, const char* line);

// Returns true if one of the regexps compiled in the array match the full request, 
// including args
DLL_PUBLIC bool requestMatches(request_rec* r, apr_array_header_t* arr);

// tokenize a string into an array, based on a character. Returns nullptr if unsuccessful
DLL_PUBLIC apr_array_header_t* tokenize(apr_pool_t* p, const char* src, char sep = '/');

// Get 3 or 4 numerical parameters from the end of the request uri
DLL_PUBLIC apr_status_t getMLRC(request_rec* r, ICD::sz5& tile, int need_m = 0);

// returns true if the If-None-Match request etag matches
DLL_PUBLIC int etagMatches(request_rec* r, const char* ETag);

// Returns an image and a 200 error code
// Sets the mime type if provided, but it doesn't overwrite an already set one
// Also sets gzip encoding if the content is gzipped and the requester handles it
// Does not handle conditional response or setting ETags, those should already be set
// src.buffer should hold at least 4 bytes
DLL_PUBLIC int sendImage(request_rec* r,
    const ICD::storage_manager& src, const char* mime_type = nullptr);

// Called with an empty tile configuration, send the empty tile with the proper ETag
// Handles conditional requests
DLL_PUBLIC int sendEmptyTile(request_rec* r, const empty_conf_t& empty);

// Parse the arguments into a key-pair hash
// Retuns a hash or NULL, if no arguments are present
// Use apr_hash_get(phash, key, APR_HASH_KEY_STRING) to get the value(s), if key is present
// returned key and pair values are url-unescaped
// If raw_args is NULL, r->args is used.  If r->args is also NULL, it returns NULL
// A key may have a null value
// If multi is true, a key may appear multiple times and the returned hash contains arrays of values
// If multi is false, the returned hash contains strings, the first appearance of each key
// It returns an empty hash if the arguments are an empty string
DLL_PUBLIC apr_hash_t* argparse(request_rec* r,
    const char* raw_args = NULL,
    const char* sep = "&",
    bool multi = false);

struct range_arg {
    range_arg() : offset(0), size(0), valid(false) {};
    apr_off_t offset;
    apr_size_t size;
    bool valid;
};

// A structure used to issue a sub-request and return the result, using mod_receive
// supports range, optional INFLATE the response, retries (for s3)
struct subr {
    subr(request_rec* r) : main(r), tries(4) {};

    // Returns APR_SUCCESS or HTTP error code
    DLL_PUBLIC int fetch(const char* url, ICD::storage_manager& dst);

    std::string agent; // input
    std::string error_message;
    std::string ETag; // output
    request_rec* main;
    range_arg range;
    int tries;
};

// Builds a MLRC URL to fetch a tile
DLL_PUBLIC char* tile_url(apr_pool_t* p, const char* src, ICD::sz5 tile, const char* suffix);

//
// Issues a subrequest to the local path and returns the content and the ETag
// Returns APR_SUCESS instead of HTTP_OK, otherwise source http response
// Returns HTTP_INTERNAL_SERVER_ERROR if mod_receive is not available
// The *psETag is allocated from r->pool, if psETag != nullptr
//
// If dst is too small but otherwise it was a success, the buffer is full and
// returns 413 (HTTP_REQUEST_ENTITY_TOO_LARGE)
//
DLL_PUBLIC int get_response(request_rec* r, const char* lcl_path, ICD::storage_manager& dst,
    char** psETag = nullptr);

// Builds an MLRC uri, suffix optional, returns "/tile</m>/L/R/C" string
DLL_PUBLIC char* pMLRC(apr_pool_t* pool, const char* prefix, const sloc_t& tile,
    const char* suffix = nullptr);

// Like get_response, but using the tile location to generate the local path
// using the M/L/R/C notation

// Builds and issues an MLRC uri, using pMLRC and get_response
static inline int get_remote_tile(request_rec* r, const char* remote, const sloc_t& tile,
    ICD::storage_manager& dst, char** psETag, const char* suffix)
{
    return get_response(r, pMLRC(r->pool, remote, tile, suffix), dst, psETag);
}

// Issues a range read to URL, based on offset and dst.size
// Returns the size of the whole file or 0 on error
// If msg is not null, *msg on return will be a error message string
DLL_PUBLIC apr_size_t range_read(request_rec* r, const char* url, apr_off_t offset,
    ICD::storage_manager& dst, int tries = 4, const char** msg = nullptr);

//  TEMPLATES

// Fetch the request configuration if it exists, otherwise the per_directory one
template<typename T> static T* get_conf(request_rec* const r, const module* const thism) {
    T* cfg = (T*)ap_get_module_config(r->request_config, thism);
    if (cfg) return cfg;
    return (T*)ap_get_module_config(r->per_dir_config, thism);
}

// build an object on pool, suitable for "create_dir_conf"
template<typename T> void* pcreate(apr_pool_t* p, char* /* path */) {
    return apr_pcalloc(p, sizeof(T));
}

// command function to set the source and suffix fields in an ahtse module configuration
template<typename T> const char* set_source(cmd_parms* cmd, T* cfg,
    const char* src, const char* suffix)
{
    cfg->source = apr_pstrdup(cmd->pool, src);
    if (suffix && suffix[0])
        cfg->suffix = apr_pstrdup(cmd->pool, suffix);
    return NULL;
}

// command function to add a regexp to the configuration
template<typename T> const char* set_regexp(cmd_parms* cmd, T* cfg, const char* pattern) {
    return add_regexp_to_array(cmd->pool, &cfg->arr_rxp, pattern);
}

NS_AHTSE_END
#endif