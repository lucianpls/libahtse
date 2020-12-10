/*
* ahtse_util.cpp
*
* libahtse implementation
*
*
* Can't use the logging functions from a shared library, the macros don't resolve correctly
* The alternative used is to return strings containing the error message, leaving the 
* called decide if logging is necessary
*
* (C) Lucian Plesea 2019-2020
*
*/

#define NOMINMAX 1
#include "ahtse.h"
#include "receive_context.h"

// httpd.h includes the ap_ headers in the right order
// It should not be needed here
#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_strings.h>
#include <ap_regex.h>
#include <http_log.h>

// strod
#include <cstdlib>
// ilogb
#include <cmath>
// setlocale
#include <clocale>
#include <cstring>
#include <string>

#include <algorithm>
#include <unordered_map>

// Need zlib to ungzip compressed input
// The apache inflate filter doesn't activate on subrequests, it can't be used
#include <zlib.h>

NS_AHTSE_START

// Given a data type name, returns a data type
GDALDataType getDT(const char *name)
{
    if (name == nullptr) return GDT_Byte;
    if (!apr_strnatcasecmp(name, "UINT16"))
        return GDT_UInt16;
    if (!apr_strnatcasecmp(name, "INT16") || !apr_strnatcasecmp(name, "SHORT"))
        return GDT_Int16;
    if (!apr_strnatcasecmp(name, "UINT32"))
        return GDT_UInt32;
    if (!apr_strnatcasecmp(name, "INT32") || !apr_strnatcasecmp(name, "INT"))
        return GDT_Int32;
    if (!apr_strnatcasecmp(name, "FLOAT32") || !apr_strnatcasecmp(name, "FLOAT"))
        return GDT_Float32;
    if (!apr_strnatcasecmp(name, "FLOAT64") || !apr_strnatcasecmp(name, "DOUBLE"))
        return GDT_Float64;
    else
        return GDT_Byte;
}

IMG_T getFMT(const std::string &sfmt) {
    if (sfmt == "image/jpeg")
        return IMG_JPEG;
    if (sfmt == "image/png")
        return IMG_PNG;
    if (sfmt == "raster/lerc")
        return IMG_LERC;
    return IMG_INVALID;
}

int GDTGetSize(GDALDataType dt, int n) {
    // It is not a multimap, so it doesn't take synonyms
    static const std::unordered_map<GDALDataType, int> size_by_gdt = {
        {GDT_Unknown, -1},
        {GDT_Byte, 1},
        {GDT_UInt16, 2},
        {GDT_Int16, 2},
        {GDT_UInt32, 4},
        {GDT_Int32, 4},
        {GDT_Float32, 4},
        {GDT_Double, 8}
    };
    return n * ((size_by_gdt.count(dt) == 0) ? -1 : size_by_gdt.at(dt));
}

// Returns NULL if it worked as expected, returns a four integer value from 
// "x y", "x y z" or "x y z c"
const char *get_xyzc_size(sz *size, const char *value)
{
    char *s;
    if (!(size && value))
        return " values missing";
    size->x = apr_strtoi64(value, &s, 0);
    size->y = apr_strtoi64(s, &s, 0);
    size->c = 3;
    size->z = 1;
    if (errno == 0 && *s != 0) {
        // Read optional third and fourth integers
        size->z = apr_strtoi64(s, &s, 0);
        if (*s != 0)
            size->c = apr_strtoi64(s, &s, 0);
    }
    if (errno != 0 || *s != 0) {
        // Raster size is 4 params max
        return " incorrect format";
    }
    return nullptr;
}

const char *add_regexp_to_array(apr_pool_t *p, apr_array_header_t **parr, const char *pattern)
{
    if (nullptr == *parr)
        *parr = apr_array_make(p, 2, sizeof(ap_rxplus_t *));
    auto m = ap_rxplus_compile(p, pattern);
    if (!m)
        return "Bad regular expression";
    APR_ARRAY_PUSH(*parr, ap_rxplus_t *) = m;
    return nullptr;
}

// Read a Key Value text file into a table
// Empty lines and lines that start with # are ignored
//
apr_table_t *readAHTSEConfig(apr_pool_t *pool, const char *fname, const char **err_message)
{
    *err_message = nullptr;
    ap_configfile_t *cfg_file;

    apr_status_t s = ap_pcfg_openfile(&cfg_file, pool, fname);
    if (APR_SUCCESS != s) { // %pm prints error string from the status
        *err_message = apr_psprintf(pool, "%s - %pm", fname, &s);
        return nullptr;
    }

    char buffer[MAX_STRING_LEN]; // MAX_STRING_LEN is from httpd.h, 8192
    apr_table_t *table = apr_table_make(pool, 8);
    // This can return ENOSPC if input lines are too long
    while (APR_SUCCESS == (s = ap_cfg_getline(buffer, MAX_STRING_LEN, cfg_file))) {
        if (strlen(buffer) == 0 || buffer[0] == '#')
            continue;
        const char *value = buffer;
        char *key = ap_getword_white(pool, &value);
        apr_table_add(table, key, value);
    }

    ap_cfg_closefile(cfg_file);
    if (s == APR_ENOSPC) {
        *err_message = apr_psprintf(pool, "input line longer than %d", MAX_STRING_LEN);
        return nullptr;
    }

    return table;
}

static void init_rsets(apr_pool_t *pool, TiledRaster &raster) {
    ap_assert(raster.pagesize.z == 1);

    rset level;
    level.rx = (raster.bbox.xmax - raster.bbox.xmin) / raster.size.x;
    level.ry = (raster.bbox.ymax - raster.bbox.ymin) / raster.size.y;
    level.w = static_cast<int>(1 + (raster.size.x - 1) / raster.pagesize.x);
    level.h = static_cast<int>(1 + (raster.size.y - 1) / raster.pagesize.y);
    level.tiles = 0;

    // How many levels are there?
    raster.n_levels = 2 + ilogb(std::max(level.w, level.h) - 1);
    raster.rsets = reinterpret_cast<rset *>(apr_pcalloc(pool,
        sizeof(rset) * raster.n_levels));

    // Populate rsets from the bottom up, the way tile protocols count levels
    // That way rset[0] matches the level 0
    // These are the MRF levels, some of the top ones might be skipped
    rset *r = &raster.rsets[raster.n_levels - 1];
    for (int i = 0; i < raster.n_levels; i++) {
        *r-- = level;
        level.tiles += raster.size.z * level.w * level.h;
        level.w = 1 + (level.w - 1) / 2;
        level.h = 1 + (level.h - 1) / 2;
        level.rx *= 2;
        level.ry *= 2;
    }

    // MRF has to have only one tile on top
    ap_assert(raster.rsets[0].h * raster.rsets[0].w == 1);
    ap_assert(raster.n_levels > raster.skip);
}

// Get a number value, forced c locale
static double get_value(const char *s, int *has) {
    double value = 0.0;
    *has = 0;
    if (s != nullptr && *s != 0) {
        *has = 1;
        const char *lcl = setlocale(LC_NUMERIC, nullptr);
        setlocale(LC_NUMERIC, "C");
        value = strtod(s, nullptr);
        setlocale(LC_NUMERIC, lcl);
    }
    return value;
}

// Consistency checks
static const char* checkRaster(const TiledRaster& raster) {
    if (IMG_INVALID == raster.format)
        return "Invalid format";

    if (IMG_PNG == raster.format) {
        if (2 < GDTGetSize(raster.datatype))
            return "Invalid DataType for PNG";
    }

    return nullptr;
}

// Initialize a raster structure from a kvp table
const char *configRaster(apr_pool_t *pool, apr_table_t *kvp, TiledRaster &raster)
{
    const char *line;
    const char *err_message;
    if (nullptr == (line = apr_table_get(kvp, "Size")))
        return "Size directive is mandatory";

    if (nullptr != (err_message = get_xyzc_size(&raster.size, line)))
        return apr_pstrcat(pool, "Size ", err_message, NULL);

    // Optional page size, default to 512x512x1xc
    raster.pagesize = {512, 512, 1, raster.size.c, raster.size.l};

    if (nullptr != (line = apr_table_get(kvp, "PageSize"))
        && nullptr != (err_message = get_xyzc_size(&raster.pagesize, line)))
            return apr_pstrcat(pool, "PageSize ", err_message, NULL);

    raster.maxtilesize = MAX_TILE_SIZE;
    if (nullptr != (line = apr_table_get(kvp, "MaxTileSize"))) {
        raster.maxtilesize = int(apr_atoi64(line));
        // Complain if proposed size is too small or too large
        if (raster.maxtilesize < 131072 || raster.maxtilesize >(1024 * 1024 * 512))
            return "MaxTileSize should be between 128K and 512M";
    }

    // This sets Byte as the default
    raster.datatype = getDT(apr_table_get(kvp, "DataType"));

    // Following fields are optional, sometimes ignored on purpose
    if (nullptr != (line = apr_table_get(kvp, "SkippedLevels")))
        raster.skip = int(apr_atoi64(line));

    line = apr_table_get(kvp, "Projection");
    raster.projection = line ? apr_pstrdup(pool, line) : "SELF";

    if (nullptr != (line = apr_table_get(kvp, "NoDataValue")))
        raster.ndv = get_value(line, &raster.has_ndv);

    if (nullptr != (line = apr_table_get(kvp, "MinValue")))
        raster.min = get_value(line, &raster.has_min);

    if (nullptr != (line = apr_table_get(kvp, "MaxValue")))
        raster.max = get_value(line, &raster.has_max);

    raster.format = (GDT_Byte == raster.datatype) ? IMG_ANY : IMG_LERC;
    if (nullptr != (line = apr_table_get(kvp, "Format")))
        raster.format = getFMT(line);

    if (IMG_LERC == raster.format) {
        int user_set = false; // See if it worked
        if (nullptr != (line = apr_table_get(kvp, "Precision"))) {
            raster.precision = get_value(line, &user_set);
        }
        if (!user_set)
            raster.precision = raster.datatype < GDT_Float ? 0.5 : 0.01;
    }

    raster.bbox.xmin = raster.bbox.ymin = 0.0;
    raster.bbox.xmax = raster.bbox.ymax = 1.0;
    if (nullptr != (line = apr_table_get(kvp, "BoundingBox"))
        && nullptr != (err_message = getBBox(line, raster.bbox)))
            return apr_pstrcat(pool, "BoundingBox ", err_message, NULL);

    if (nullptr != (line = apr_table_get(kvp, "ETagSeed"))) {
        // Ignore the flag when reading in the seed
        int flag;
        raster.seed = base32decode(line, &flag);
        // Set the missing tile etag, with the flag set because it is the empty tile etag
        tobase32(raster.seed, raster.missing.eTag, 1);
    }

    init_rsets(pool, raster);

    return checkRaster(raster);
}

const char *getBBox(const char *line, bbox_t &bbox)
{
    const char *lcl = setlocale(LC_NUMERIC, NULL);
    const char *message = "incorrect format, expecting four comma separated C locale numbers";

    char *l;
    setlocale(LC_NUMERIC, "C");
    bbox.xmin = strtod(line, &l);
    if (*l++ != ',') goto done;
    bbox.ymin = strtod(l, &l);
    if (*l++ != ',') goto done;
    bbox.xmax = strtod(l, &l);
    if (*l++ != ',') goto done;
    bbox.ymax = strtod(l, &l);
    message = nullptr;

done:
    setlocale(LC_NUMERIC, lcl);
    return message;
}

// Return the value from a base 32 character
// Returns a negative value if char is not a valid base32 char
// ASCII only
static int b32(char ic) {
    int c = 0xff & (static_cast<int>(ic));
    if (c < '0') return -1;
    if (c - '0' < 10) return c - '0';
    if (c < 'A') return -1;
    if (c - 'A' < 22) return c - 'A' + 10;
    if (c < 'a') return -1;
    if (c - 'a' < 22) return c - 'a' + 10;
    return -1;
}

apr_uint64_t base32decode(const char *is, int *flag) {
    apr_int64_t value = 0;
    const unsigned char *s = reinterpret_cast<const unsigned char *>(is);
    while (*s == static_cast<unsigned char>('"'))
        s++; // Skip quotes
    *flag = b32(*s) & 1; // Pick up the nodata flag, least bit of first char
                         // Initial value ignores the flag
    int digits = 0; // How many base32 digits we've seen
    for (int v = (b32(*s++) >> 1); v >= 0 && digits <= 13; v = b32(*s++), digits++)
        value = (value << 5) + v;
    // Assume trailing zeros are missing if digits < 13
    if (digits < 13)
        value <<= 5 * (13 - digits);
    return value;
}

void tobase32(apr_uint64_t value, char *buffer, int b65) {
    static char b32digits[] = "0123456789abcdefghijklmnopqrstuv";
    // First char holds the 65th bit, which is stored as if it would be in position 60!
    buffer[0] = b32digits[(((value >> 60) & 0xf) << 1) | (b65 ? 1 : 0)];
    // Five bits at a time, from the top, 60 bits in groups of 5
    for (int i = 1; i < 13; i++)
        buffer[i] = b32digits[(value >> (60 - i * 5)) & 0x1f];
    buffer[13] = '\0';
}

// Read a file, or a portion of a file in the storage manager
// the line is of the format
//
// <size> <offset> fname
// size defaults to full file size and offset defaults to 0
//
char *readFile(apr_pool_t *pool, storage_manager &mgr, const char *line)
{
    apr_file_t *efile;
    apr_off_t offset = 0;
    apr_status_t stat;
    char *last;

    mgr.size = static_cast<int>(apr_strtoi64(line, &last, 0));
    // Might be an offset, or offset then file name
    if (last != line)
        apr_strtoff(&(offset), last, &last, 0);

    while (*last && isblank(*last)) last++;
    const char *efname = last;

    // Use the temp pool for the file open, it will close it for us
    if (0 == mgr.size) { // Don't know the size, get it from the file
        apr_finfo_t finfo;
        stat = apr_stat(&finfo, efname, APR_FINFO_CSIZE, pool);
        if (APR_SUCCESS != stat)
            return apr_psprintf(pool, "Can't stat %s %pm", efname, &stat);
        mgr.size = static_cast<int>(finfo.size);
    }

    apr_size_t size = mgr.size;
    if (size > MAX_READ_SIZE)
        return apr_psprintf(pool, "Empty tile too large, max is %d", MAX_READ_SIZE);

    stat = apr_file_open(&efile, efname, READ_RIGHTS, 0, pool);
    if (APR_SUCCESS != stat)
        return apr_psprintf(pool, "Can't open empty file %s, %pm", efname, &stat);
    mgr.buffer = static_cast<char *>(apr_palloc(pool, size));
    stat = apr_file_seek(efile, APR_SET, &offset);
    if (APR_SUCCESS != stat)
        return apr_psprintf(pool, "Can't seek empty tile %s: %pm", efname, &stat);
    stat = apr_file_read(efile, mgr.buffer, &size);
    if (APR_SUCCESS != stat || size != static_cast<apr_size_t>(mgr.size))
        return apr_psprintf(pool, "Can't read from %s: %pm", efname, &stat);
    apr_file_close(efile);
    return NULL;
}

bool requestMatches(request_rec *r, apr_array_header_t *arr) {
    if (nullptr == arr || nullptr == r)
        return false;

    // Match the request, including the arguments if present
    const char *url_to_match = r->args ? 
        apr_pstrcat(r->pool, r->uri, "?", r->args, NULL) : r->uri;
    for (int i = 0; i < arr->nelts; i++)
        if (ap_rxplus_exec(r->pool, APR_ARRAY_IDX(arr, i, ap_rxplus_t *), 
                            url_to_match, nullptr))
            return true;

    return false;
}

apr_array_header_t *tokenize(apr_pool_t *p, const char *src, char sep) {
    apr_array_header_t *arr = nullptr;
    // Skip the separators from the start of the string
    while (sep == *src)
        src++;

    while (*src != 0) {
        char *val = ap_getword(p, &src, sep);
        if (nullptr == arr)
            arr = apr_array_make(p, 10, sizeof(char *));
        APR_ARRAY_PUSH(arr, char *) = val;
    }
    return arr;
}

// Sends an image, sets the output mime_type.
// If mime_type is empty or "auto", it can detect the type based on 32bit signature
// ETag should be set before
// Any return other than OK is an error sign
int sendImage(request_rec *r, const storage_manager &src, const char *mime_type)
{
    // Simple case first
    if (nullptr == src.buffer)
        return HTTP_NOT_FOUND;

    apr_uint32_t sig = *reinterpret_cast<apr_int32_t *>(src.buffer);
    if (mime_type == nullptr || apr_strnatcmp(mime_type, "auto")) {
        // Set the type based on signature
        switch (sig) {
        case JPEG_SIG:
            mime_type = "image/jpeg";
            break;
        case PNG_SIG:
            mime_type = "image/png";
            break;
        default: // LERC and others go here
            mime_type = "application/octet";
        }
    }
    ap_set_content_type(r, mime_type);

    if (GZIP_SIG == sig) {
        apr_table_setn(r->headers_out, "Content-Encoding", "gzip");
        const char *ae = apr_table_get(r->headers_in, "Accept-Encoding");
        // If accept encoding is missing, assume it doesn't support gzip
        if (!ae || !strstr(ae, "gzip")) {
            ap_filter_rec_t *inflate_filter = ap_get_output_filter_handle("INFLATE");
            // Should flag this as an error, but how?
            if (!inflate_filter)
                return HTTP_INTERNAL_SERVER_ERROR;

            ap_add_output_filter_handle(inflate_filter, NULL, r, r->connection);
        }
    }

    // Finally, the data itself
    ap_set_content_length(r, src.size);
    ap_rwrite(src.buffer, src.size, r);
    ap_rflush(r);
    // Response is done
    return OK;
}

// Called with an empty tile configuration, send the empty tile with the proper ETag
// Handles conditional requests
int sendEmptyTile(request_rec *r, const empty_conf_t &empty) {
    if (etagMatches(r, empty.eTag)) {
        apr_table_setn(r->headers_out, "ETag", empty.eTag);
        return HTTP_NOT_MODIFIED;
    }

    if (nullptr == empty.data.buffer)
        return DECLINED;

    apr_table_setn(r->headers_out, "ETag", empty.eTag);
    return sendImage(r, empty.data);
}

apr_status_t getMLRC(request_rec *r, sz &tile, int need_m) {
    auto *tokens = tokenize(r->pool, r->uri);
    if (tokens->nelts < 3 || (need_m && tokens->nelts < 4))
        return APR_BADARG;

    tile.x = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    tile.y = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    tile.l = apr_atoi64(ARRAY_POP(tokens, char*)); if (errno) return errno;
    tile.z = need_m ? apr_atoi64(ARRAY_POP(tokens, char *)) : 0;
    // Z defaults to 0
    if (errno) tile.z = 0;
    return APR_SUCCESS;
}

// These are very small, they should be static inlines, not DLL_PUBLIC
int etagMatches(request_rec *r, const char *ETag) {
    const char *ETagIn = apr_table_get(r->headers_in, "If-None-Match");
    return (nullptr != ETagIn && 0 != strstr(ETagIn, ETag));
}

int getBool(const char *s) {
    while (*s != 0 && (*s == ' ' || *s == '\t'))
        s++;
    return (!ap_cstr_casecmp(s, "On") || !ap_cstr_casecmp(s, "True") || *s == '1');
}

// Parser for kev=value pair string
// adapted from "the Apache Modules book"
// If multi is set, a key instance may be repeated and the returned hash 
// contains arrays of values. Otherwise only the first appearance of a key 
// is kept and the returned hash contains strings
apr_hash_t *argparse(request_rec *r, const char *raw_args, const char *sep, bool multi)
{
    if (!raw_args)
        raw_args = r->args;
    if (!raw_args)
        return NULL;

    // Use a copy of the arguments
    char *args = apr_pstrdup(r->pool, raw_args);
    apr_hash_t *form = apr_hash_make(r->pool);
    char *last = NULL, *pair = NULL;
    while ((pair = apr_strtok(args, sep, &last))) {
        args = NULL;
        for (char *c = pair; *c; c++)
            if (*c == '+') *c = ' ';
        // split the argument into key and value, unescape them
        char *v = strchr(pair, '=');
        if (v) {
            *v++ = 0;
            ap_unescape_url(v);
        }
        ap_unescape_url(pair);

        // Set this key-value
        if (!multi) {
            if (!apr_hash_get(form, pair, APR_HASH_KEY_STRING))
                apr_hash_set(form, pair, APR_HASH_KEY_STRING, v);
            continue;
        }

        // Store key/value pair in the form hash.  
        // Since a key may be repeated, store the values in an array
        auto *values = reinterpret_cast<apr_array_header_t *>(
            apr_hash_get(form, pair, APR_HASH_KEY_STRING));
        if (!values) { // Create the values array
            values = apr_array_make(r->pool, 1, sizeof(void *));
            apr_hash_set(form, pair, APR_HASH_KEY_STRING, values);
        }
        APR_ARRAY_PUSH(values, char *) = v;
    }

    return form;
 }

// Mostly copied from mrf_util.cpp:ZUnPack()
// return true if all is OK
static int ungzip(const storage_manager& src, storage_manager& dst) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = reinterpret_cast<Bytef*>(src.buffer);
    stream.avail_in = static_cast<uInt>(src.size);
    stream.next_out = reinterpret_cast<Bytef*>(dst.buffer);
    stream.avail_out = static_cast<uInt>(dst.size);

    // Gzip, max window size
    if (Z_OK != inflateInit2(&stream, 16 + MAX_WBITS))
        return false;
    auto err = inflate(&stream, Z_FINISH);

    // Use dst.size as flag, that buffer was too small
    if (Z_BUF_ERROR == err)
        dst.size = -1; 

    if (Z_STREAM_END != err) {
        inflateEnd(&stream);
        return false;
    }

    dst.size = stream.total_out;
    return Z_OK == inflateEnd(&stream);
}

// DEBUG, log headers in debug mode, call with 
// apr_table_do(phdr, r, header_table, NULL)
//
//static int phdr(void *rec, const char *key, const char *value) {
//    request_rec* r = (request_rec*)rec;
//    ap_log_rerror(__FILE__, __LINE__, APLOG_NO_MODULE, APLOG_DEBUG, 0, r, "%s=%s", key, value);
//    return 1;
//}
//

// Remove the quotes, keep just the value
//static void cleanTag(std::string& tag) {
//    if ('"' == *tag.begin())
//        tag.erase(0, 1);
//    if ('"' == *tag.rbegin())
//        tag.erase(tag.end() - 1);
//}

int subr::fetch(const char *url, storage_manager& dst) {
    static ap_filter_rec_t* receive_filter = nullptr;
    if (!receive_filter) {
        receive_filter = ap_get_output_filter_handle("Receive");
        if (!receive_filter)
            return HTTP_INTERNAL_SERVER_ERROR; // Receive not found
    }

    int failed = false;
    char* srange = nullptr;
    if (range.valid) 
        srange = apr_psprintf(main->pool, "bytes=%" APR_UINT64_T_FMT "-%" APR_UINT64_T_FMT,
                    range.offset, range.size);

    // Keep this outside of the loop, so we can capture rctx.maxsize
    receive_ctx rctx;
    // For Etag capture
    uint64_t evalue = 0;
    int missing = 0;
    do {
        rctx.buffer = dst.buffer;
        rctx.maxsize = dst.size;
        rctx.size = 0;

        request_rec* sr = ap_sub_req_lookup_uri(url, main, main->output_filters);
        if (range.valid)
            apr_table_setn(sr->headers_in, "Range", srange);
        if (!agent.empty())
            apr_table_setn(sr->headers_in, "User-Agent", agent.c_str());
        ap_filter_t* rf = 
            ap_add_output_filter_handle(receive_filter, &rctx, sr, sr->connection);
        int status = ap_run_sub_req(sr);
        int sr_status = sr->status;

        // input ETag, if any, before destroying subrequest
        const char *intag = apr_table_get(sr->headers_out, "ETag");
        if (intag)
            evalue = base32decode(intag, &missing);

        ap_remove_output_filter(rf);
        ap_destroy_sub_req(sr);

        if (APR_SUCCESS != status) {
            failed = true; // the request didn't work!
            break;
        }

        // exit condition, got what we need
        if ((range.valid && static_cast<size_t>(rctx.size) == range.size) 
            || (!range.valid && HTTP_OK == sr_status)) {
            dst.size = rctx.size;
            break;
        }

        switch (sr_status) {
        case HTTP_OK:
            break;
        case HTTP_PARTIAL_CONTENT: // The only time we retry
            if (0 == tries--) {
                error_message = "Retries exhausted";
                failed = true;
            }
            break;
        default:
            error_message = apr_psprintf(main->pool, "Remote responds with %d", sr_status);
            failed = true;
        }

    } while (!failed);

    // Build an etag from raw content, if it's large enough
    if (!evalue && dst.size > 128) {
        evalue = *(reinterpret_cast<uint64_t*>(dst.buffer) + 4);
        evalue |= *(reinterpret_cast<uint64_t*>(dst.buffer) + dst.size / 8 - 4);
        evalue ^= *(reinterpret_cast<uint64_t*>(dst.buffer) + dst.size / 8 - 6);
    }


    char etagsrc[14] = { 0 };
    tobase32(evalue, etagsrc, missing);
    ETag = etagsrc;

    uint32_t sig = 0;
    if (!failed && static_cast<size_t>(dst.size) >= sizeof(sig))
        memcpy(&sig, dst.buffer, sizeof(sig));

    // This needs to do an in-place unzip
    if (GZIP_SIG == sig) { // !failed is implicit, so we can return
        // Try using the reminder of the buffer
        storage_manager zipdest;
        zipdest.buffer = dst.buffer + dst.size;
        zipdest.size = rctx.maxsize - dst.size;

        if (!ungzip(dst, zipdest)) {
            // Maybe too large, allocate a new buffer, unpack there, then copy data back
            // the unpacked size still needs to be under the input dest buffer max size
            if (dst.size < 0) { // Output buffer was too small
                zipdest.size = rctx.maxsize;
                zipdest.buffer = reinterpret_cast<char*>(apr_palloc(main->pool, zipdest.size));
                failed = !ungzip(dst, zipdest);
                if (failed)
                    error_message = "Uncompressed output buffer too small";
            }
            else { // Some other unzip error
                error_message = "ungzip error";
                failed = true;
            }
        }

        if (!failed) {
            memmove(dst.buffer, zipdest.buffer, zipdest.size);
            dst.size = zipdest.size;
        }
    }

    return failed ? HTTP_NOT_FOUND : APR_SUCCESS;
}

DLL_PUBLIC char* tile_url(apr_pool_t* p, const char* src, sz tile, const char* suffix) {
    if (!src || !strlen(src))
        return nullptr; // Error
    const char* slash = src[strlen(src)-1] == '/' ? "" : "/";
    return apr_pstrcat(p, src, 
        tile.z ? apr_psprintf(p, "%s%d/", slash, static_cast<int>(tile.z)) : slash,
        apr_psprintf(p, "%d/%d/%d", static_cast<int>(tile.l), 
            static_cast<int>(tile.y), static_cast<int>(tile.x)),
        suffix, nullptr);
}

// Issues a subrequest and captures the response and the ETag
int get_response(request_rec *r, const char *lcl_path, storage_manager &dst,
    char **psETag)
{
    static ap_filter_rec_t *receive_filter = nullptr;
    if (!receive_filter) {
        receive_filter = ap_get_output_filter_handle("Receive");
        if (!receive_filter)
            return HTTP_INTERNAL_SERVER_ERROR; // Receive not found
    }

    request_rec *sr = ap_sub_req_lookup_uri(lcl_path, r, r->output_filters);
    // if status is not 200 here, no point in going further
    if (sr->status != HTTP_OK)
        return sr->status;

    receive_ctx rctx;
    rctx.buffer = dst.buffer;
    rctx.maxsize = dst.size;
    rctx.size = 0;
    rctx.overflow = 0;

    ap_filter_t *rf = ap_add_output_filter_handle(receive_filter, &rctx,
        sr, sr->connection);
    auto code = ap_run_sub_req(sr); // This returns SUCCESS most of the time
    auto status = sr->status;
    // If code is not SUCCESS, then it is the status
    if (OK != code)
        status = code;
    dst.size = rctx.size;
    const char *sETag = apr_table_get(sr->headers_out, "ETag");
    if (psETag && sETag)
        *psETag = apr_pstrdup(r->pool, sETag);
    ap_remove_output_filter(rf);
    ap_destroy_sub_req(sr);
    return 200 == status ? APR_SUCCESS: status;  // returns APR_SUCCESS or http code
}

// Builds an MLRC uri, suffix optional
char *pMLRC(apr_pool_t *pool, const char *prefix, const sloc_t &tile, const char *suffix) {
#define FMT APR_INT64_T_FMT
    char *stile = apr_psprintf(pool, "/%" FMT "/%" FMT "/%" FMT "/%" FMT,
        tile.z, tile.l, tile.y, tile.x);
#undef FMT
    if (0 == tile.z)
        stile += 2;
    return apr_pstrcat(pool, prefix, "/tile", stile, suffix, NULL);
}

int range_read(request_rec *r, const char *url, apr_off_t offset,
    storage_manager &dst, int tries, const char **msg)
{
    ap_filter_rec_t *receive_filter = nullptr;
    ap_get_output_filter_handle("Receive");
    if (!receive_filter) {
        receive_filter = ap_get_output_filter_handle("Receive");
        if (!receive_filter) {
            if (msg)
                *msg = "Receive filter not found";
            return 0; // Receive not found
        }
    }

    receive_ctx rctx;
    rctx.buffer = dst.buffer;
    rctx.maxsize = dst.size;
    rctx.size = 0;

    char *srange = apr_psprintf(r->pool,
        "bytes=%" APR_UINT64_T_FMT "-%" APR_UINT64_T_FMT,
        offset, offset + dst.size);

    // S3 may return less than requested, so we retry the request a couple of times
    bool failed = false;
    do {
        request_rec *sr = ap_sub_req_lookup_uri(url, r, r->output_filters);
        apr_table_setn(sr->headers_in, "Range", srange);
        ap_filter_t *rf = ap_add_output_filter_handle(receive_filter, &rctx,
            sr, sr->connection);
        int status = ap_run_sub_req(sr);
        int sr_status = sr->status;
        ap_remove_output_filter(rf);
        ap_destroy_sub_req(sr);

        failed = !(APR_SUCCESS == status);
        if (!failed) {
            switch (sr_status) {
            case HTTP_PARTIAL_CONTENT:
                if (0 == tries--) {
                    *msg = "Retries exhausted";
                    failed = true;
                }
            // TODO follow redirects
            case HTTP_OK:
                break;
            default: // Any other return code is unrecoverable
                *msg = apr_psprintf(r->pool, "Remote responds with %d", sr_status);
                failed = true;
            }
        }
    } while (!failed && rctx.size != dst.size);

    return failed ? 0 : rctx.size;
}

const char* stride_decode(codec_params& params, storage_manager& src, void* buffer)
{
    const char* error_message = nullptr;
    apr_uint32_t sig = 0;
    memcpy(&sig, src.buffer, sizeof(sig));
    params.format = IMG_INVALID;
    switch (sig)
    {
    case JPEG_SIG:
        error_message = jpeg_stride_decode(params, src, buffer);
        params.format = IMG_JPEG;
        break;
    case PNG_SIG:
        error_message = png_stride_decode(params, src, buffer);
        params.format = IMG_PNG;
        break;
    case LERC_SIG:
        error_message = lerc_stride_decode(params, src, buffer);
        params.format = IMG_LERC;
        break;
    default:
        error_message = "Decode requested for unknown format";
    }
    return error_message;
}

NS_AHTSE_END
