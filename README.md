# libahtse

[AHTSE](https://github.com/lucianpls/AHTSE) library, C++, to be shared between various AHTSE modules

Includes image codecs for JPEG, JPEG12, PNG and LERCV1
The content of jpeg12-6b folder is used to generate a static JPEG library with 12 bit names  
The 8 bit libjpeg and libpng used are the system ones. This makes it possible to use 
jpeg-turbo for 8 bit and jpeg12-6b for 12 bit

## httpd_patches content

This folder contains a few useful patches for the Apache httpd 2.x source. [README](httpd_patches/README.md)

## AHTSE development rules

The _Source is an apache directive, used by any non-source module.  It takes one or two 
arguments, first one is the internal httpd absolute redirect path, the second one is the 
postfix, which may include http form parameters.  The set_source template function can 
be used to parse it, into the configuration _source_ and _postfix_ fields.  

## Webconf content

By convention, configuration files for AHTSE modules use the extension *webconf*. In addition to module specific parameters, there are a number of parameters that are always recognized by the library.

### Size X Y Z C
Mandatory, the size of the full resolution

### PageSize X Y Z C
Optional, the size of a tile, defaults to 512 512 1 3

### MaxTileSize N
Optional, the maximum size of a packed tile, in bytes. Defaults to 4MB

### SkippedLevels N
Number of levels at the top of the pyramid which are skipped, defaults to 0. Internally, the pyramid is always assumed to be complete, with the top level being level 0 and containing a single tile. Setting this to a higher level resets the external numbering, the external level 0 will be equivalent to the internal level N

### Projection String
Optional, defaults to SELF. Other useful values are:
- WM - Web Mercator, aka Spherical Mercator
- GCS - Global Coordinate System, aka Lat-Lon
- Mercator - WGS84 Mercator

### NoDataValue V
The value used to signify missing data.

### MinValue V

### MaxValue V

### DataType
- Byte, UInt8
- Short, Short16
- UInt16
- Int, Int32
- UInt32
- Float, Float32
- Double, Float64

If the value is not one of the above, the default value of Byte will be used

### Format
- image/jpeg
- image/png
- raster/lerc
These are used to force the output format, for ATHSE modules that reformat the data. On input, the format is self-detecting in most cases.

### BoundingBox MinX,MinY,MaxX,MaxY
Bouding box, in a WMS style format. Expects four comma separated floating point values using "." as the unit separator, in the projection coordinate system. Defaults to 0,0,1,1

### ETagSeed B32VAL
A 64 bit value as 13 base32 digits. May be used to seed the ETag tile values
