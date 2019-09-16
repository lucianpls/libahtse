# Patches for Apache httpd

## apr_fopen_random
Adds *APR_FOPEN_RANDOM* as a flag for apr_fopen, when a file needs to be opened without read-ahead.  It can improve performance on very busy servers, because it lowers the amount of IO. Use in code as appropriate, but only after testing that APR_FOPEN_RANDOM is defined. Patches the apache portable runtime source, seems to work for both apr-1 and apr-2.
## proxypass_nomain_flag
Adds a *nomain* option to the ProxyPass directive. When set, the reverse proxy of this is available for use by subrequests such as the ones used by many of the AHTSE modules, but not directly.

## mod_proxy_http_subreq_connection_reuse
When mod_proxy_http is patched with this code, the reverse proxy connection to the backend server can be reused by subrequests. Withouth this patch, any connection to a backend server by a subrequest is only used once.  Applying this patch has the effect of greatly incresing reverse proxy performance for AHTSE modules that rely on reverse proxy requests.
