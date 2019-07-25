# libahtse

AHTSE Utily library, C++, to be shared between AHTSE modules

Contains image codecs for JPEG, JPEG12 and PNG  
The content of jpeg12-6b folder is used to generate a static JPEG library with 12 bit names

## AHTSE development rules

The _Source is an apache directive, used by any non-source module.  It takes one or two 
arguments, first one is the internal httpd absolute redirect path, the second one is the 
postfix, which may include http form parameters.  The set_source template function can 
be used to parse it, into the configuration _source_ and _postfix_ fields.  
