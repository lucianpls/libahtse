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
