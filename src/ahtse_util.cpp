/*
* ahtse_util.cpp
*
* libahtse implementation
*
* (C) Lucian Plesea 2019
*/

#define NOMINMAX 1
#include "ahtse.h"

// httpd.h includes the ap_ headers in the right order
// It should not be needed here
#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <apr_strings.h>
#include <ap_regex.h>

// strod
#include <cstdlib>
// ilogb
#include <cmath>
// setlocale
#include <clocale>
#include <algorithm>
#include <unordered_map>

NS_AHTSE_START

// Given a data type name, returns a data type
GDALDataType getDT(const char *name)
{
    if (name == nullptr) return GDT_Byte;
    if (!apr_strnatcasecmp(name, "UINT16"))
        return GDT_UInt16;
    else if (!apr_strnatcasecmp(name, "INT16") || !apr_strnatcasecmp(name, "SHORT"))
        return GDT_Int16;
    else if (!apr_strnatcasecmp(name, "UINT32"))
        return GDT_UInt32;
    else if (!apr_strnatcasecmp(name, "INT32") || !apr_strnatcasecmp(name, "INT"))
        return GDT_Int32;
    else if (!apr_strnatcasecmp(name, "FLOAT32") || !apr_strnatcasecmp(name, "FLOAT"))
        return GDT_Float32;
    else if (!apr_strnatcasecmp(name, "FLOAT64") || !apr_strnatcasecmp(name, "DOUBLE"))
        return GDT_Float64;
    else
        return GDT_Byte;
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
    ap_rxplus_t **m = reinterpret_cast<ap_rxplus_t **>(apr_array_push(*parr));
    *m = ap_rxplus_compile(p, pattern);
    return (nullptr != *m) ? nullptr : "Bad regular expression";
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
    level.rx = (raster.bbox.xmax - raster.bbox.xmin) / raster.pagesize.x;
    level.ry = (raster.bbox.ymax - raster.bbox.ymin) / raster.pagesize.y;
    level.w = static_cast<int>(1 + (raster.size.x - 1) / raster.pagesize.x);
    level.h = static_cast<int>(1 + (raster.size.y - 1) / raster.pagesize.y);

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
        level.w = 1 + (level.w - 1) / 2;
        level.h = 1 + (level.h - 1) / 2;
        level.rx *= 2;
        level.ry *= 2;
    }

    // MRF has to have only one tile on top
    ap_assert(raster.rsets[0].h * raster.rsets[0].w == 1);
    ap_assert(raster.n_levels > raster.skip);
}

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

    // This sets Byte as the default
    raster.datatype = getDT(apr_table_get(kvp, "DataType"));

    // Following fields are optional, sometimes ignored on purpose
    if (nullptr != (line = apr_table_get(kvp, "SkippedLevels")))
        raster.skip = int(apr_atoi64(line));

    if (nullptr != (line = apr_table_get(kvp, "Projection")))
        raster.projection = line ? apr_pstrdup(pool, line) : "WM";

    if (nullptr != (line = apr_table_get(kvp, "NoDataValue")))
        raster.ndv = get_value(line, &raster.has_ndv);

    if (nullptr != (line = apr_table_get(kvp, "MinValue")))
        raster.min = get_value(line, &raster.has_min);

    if (nullptr != (line = apr_table_get(kvp, "MaxValue")))
        raster.max = get_value(line, &raster.has_max);

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

    return nullptr;
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
    if (*l++ != ',') goto done;
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
    *flag = b32(*s) & 1; // Pick up the flag, least bit of top char
                         // Initial value ignores the flag
    int digits = 0; // How many base32 digits we've seen
    for (int v = (b32(*s++) >> 1); v >= 0; v = b32(*s++), digits++)
        value = (value << 5) + v;
    // Trailing zeros are missing if digits < 13
    if (digits < 13)
        value <<= 5 * (13 - digits);
    return value;
}

void tobase32(apr_uint64_t value, char *buffer, int flag) {
    static char b32digits[] = "0123456789abcdefghijklmnopqrstuv";
    // First char has the flag bit
    if (flag) flag = 1; // Normalize value
    buffer[0] = b32digits[((value & 0xf) << 1) | flag];
    value >>= 4; // Encoded 4 bits
                 // Five bits at a time, 60 bytes
    for (int i = 1; i < 13; i++) {
        buffer[i] = b32digits[value & 0x1f];
        value >>= 5;
    }
    buffer[13] = '\0';
}

char *readFile(apr_pool_t *pool, storage_manager &empty, const char *line)
{
    // If we're provided a file name or a size, pre-read the empty tile in the 
    apr_file_t *efile;
    apr_off_t offset = 0;
    apr_status_t stat;
    char *last;

    empty.size = static_cast<int>(apr_strtoi64(line, &last, 0));
    // Might be an offset, or offset then file name
    if (last != line)
        apr_strtoff(&(offset), last, &last, 0);

    while (*last && isblank(*last)) last++;
    const char *efname = last;

    // Use the temp pool for the file open, it will close it for us
    if (0 == empty.size) { // Don't know the size, get it from the file
        apr_finfo_t finfo;
        stat = apr_stat(&finfo, efname, APR_FINFO_CSIZE, pool);
        if (APR_SUCCESS != stat)
            return apr_psprintf(pool, "Can't stat %s %pm", efname, &stat);
        empty.size = static_cast<int>(finfo.csize);
    }

    if (empty.size > MAX_READ_SIZE)
        return apr_psprintf(pool, "Empty tile too large, max is %d", MAX_READ_SIZE);

    stat = apr_file_open(&efile, efname, READ_RIGHTS, 0, pool);
    if (APR_SUCCESS != stat)
        return apr_psprintf(pool, "Can't open empty file %s, %pm", efname, &stat);
    empty.buffer = static_cast<char *>(apr_palloc(pool, (apr_size_t)empty.size));
    stat = apr_file_seek(efile, APR_SET, &offset);
    if (APR_SUCCESS != stat)
        return apr_psprintf(pool, "Can't seek empty tile %s: %pm", efname, &stat);
    apr_size_t size = static_cast<apr_size_t>(empty.size);
    stat = apr_file_read(efile, empty.buffer, &size);
    if (APR_SUCCESS != stat)
        return apr_psprintf(pool, "Can't read from %s: %pm", efname, &stat);
    apr_file_close(efile);
    return NULL;
}

bool requestMatches(request_rec *r, apr_array_header_t *arr) {
    if (nullptr == arr || nullptr == r)
        return false;

    // Match the request, including the arguments if present
    const char *url_to_match = r->args ? apr_pstrcat(r->pool, r->uri, "?", r->args, NULL) : r->uri;
    for (int i = 0; i < arr->nelts; i++)
        if (ap_rxplus_exec(r->pool, APR_ARRAY_IDX(arr, i, ap_rxplus_t *), url_to_match, nullptr))
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
        char **newelt = (char **)apr_array_push(arr);
        *newelt = val;
    }

    return arr;
}

// Sends an image, sets the output mime_type.
// If mime_type is empty or "auto", it can detect the type based on 32bit signature
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
    if (GZIP_SIG == sig)
        apr_table_setn(r->headers_out, "Content-Encoding", "gzip");

    // Finally, the data itself
    ap_set_content_length(r, src.size);
    ap_rwrite(src.buffer, src.size, r);
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

    if (nullptr != empty.empty.buffer)
        return DECLINED;

    apr_table_setn(r->headers_out, "ETag", empty.eTag);
    return sendImage(r, empty.empty);
}

// These are very small, they should be static inlines, not DLL_PUBLIC
int etagMatches(request_rec *r, const char *ETag) {
    const char *ETagIn = apr_table_get(r->headers_in, "If-None-Match");
    // There can be more than one code in the input etag, check for the right substring
    return (nullptr != ETagIn && strstr(ETagIn, ETag) != 0);
}

int get_bool(const char *s) {
    while (*s != 0 && (*s == ' ' || *s == '\t'))
        s++;
    return (!ap_cstr_casecmp(s, "On") || !ap_cstr_casecmp(s, "1"));
}

NS_AHTSE_END