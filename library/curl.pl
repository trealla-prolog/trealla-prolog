:- module(curl, [
	curl_easy_init/1,
	curl_easy_setopt/4,
	curl_easy_perform/2,
	curl_easy_cleanup/1,
	curlopt/3
	]).

:- use_foreign_module('libcurl.so', [
	curl_easy_init([], ptr),
	curl_easy_setopt([ptr,uint,...,ptr], sint),
	curl_easy_perform([ptr], sint),
	curl_easy_cleanup([ptr], void)
	]).

% Zero return status means everything was ok, non-zero means an error occured.

curlopt(xCURLOPT_WRITEDATA, xCURLOPTTYPE_CBPOINT, 10001) :- !.		% HACK
curlopt(xCURLOPT_URL, xCURLOPTTYPE_STRINGPOINT, 10002) :- !.		% HACK
curlopt(xCURLOPT_READDATA, xCURLOPTTYPE_CBPOINT, 10009) :- !.		% HACK
curlopt(xCURLOPT_REFERER, xCURLOPTTYPE_STRINGPOINT, 10016) :- !.	% HACK
curlopt(xCURLOPT_USERAGENT, xCURLOPTTYPE_STRINGPOINT, 10018) :- !.	% HACK
curlopt(xCURLOPT_COOKIE, xCURLOPTTYPE_STRINGPOINT, 10022) :- !.		% HACK

curlopt(xCURLOPT_TIMEOUT, xCURLOPTTYPE_LONG, 13) :- !.				% HACK
curlopt(xCURLOPT_VERBOSE, xCURLOPTTYPE_LONG, 41) :- !.				% HACK
curlopt(xCURLOPT_NOBODY, xCURLOPTTYPE_LONG, 44) :- !.				% HACK
curlopt(xCURLOPT_POST, xCURLOPTTYPE_LONG, 47) :- !.					% HACK
curlopt(xCURLOPT_PUT, xCURLOPTTYPE_LONG, 54) :- !.					% HACK

% TODO: fix this...

/* This is the FILE * or void * the regular output should be written to. */
curlopt(xCURLOPT_WRITEDATA, xCURLOPTTYPE_CBPOINT, 1).

/* The full URL to get/put */
curlopt(xCURLOPT_URL, xCURLOPTTYPE_STRINGPOINT, 2).

/* Port number to connect to, if other than default. */
curlopt(xCURLOPT_PORT, xCURLOPTTYPE_LONG, 3).

/* Name of proxy to use. */
curlopt(xCURLOPT_PROXY, xCURLOPTTYPE_STRINGPOINT, 4).

/* "user:password;options" to use when fetching. */
curlopt(xCURLOPT_USERPWD, xCURLOPTTYPE_STRINGPOINT, 5).

/* "user:password" to use with proxy. */
curlopt(xCURLOPT_PROXYUSERPWD, xCURLOPTTYPE_STRINGPOINT, 6).

/* Range to get, specified as an ASCII string. */
curlopt(xCURLOPT_RANGE, xCURLOPTTYPE_STRINGPOINT, 7).

/* not used */

/* Specified file stream to upload from (use as input): */
curlopt(xCURLOPT_READDATA, xCURLOPTTYPE_CBPOINT, 9).

/* Buffer to receive error messages in, must be at least CURL_ERROR_SIZE
* bytes big. */
curlopt(xCURLOPT_ERRORBUFFER, xCURLOPTTYPE_OBJECTPOINT, 10).

/* Function that will be called to store the output (instead of fwrite). The
* parameters will use fwrite() syntax, make sure to follow them. */
curlopt(xCURLOPT_WRITEFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 11).

/* Function that will be called to read the input (instead of fread). The
* parameters will use fread() syntax, make sure to follow them. */
curlopt(xCURLOPT_READFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 12).

/* Time-out the read operation after this amount of seconds */
curlopt(xCURLOPT_TIMEOUT, xCURLOPTTYPE_LONG, 13).

/* If the xCURLOPT_INFILE is used, this can be used to inform libcurl about
* how large the file being sent really is. That allows better error
* checking and better verifies that the upload was successful. -1 means
* unknown size.
*
* For large file support, there is also a _LARGE version of the key
* which takes an off_t type, allowing platforms with larger off_t
* sizes to handle larger files.  See below for INFILESIZE_LARGE.
*/
curlopt(xCURLOPT_INFILESIZE, xCURLOPTTYPE_LONG, 14).

/* POST static input fields. */
curlopt(xCURLOPT_POSTFIELDS, xCURLOPTTYPE_OBJECTPOINT, 15).

/* Set the referrer page (needed by some CGIs) */
curlopt(xCURLOPT_REFERER, xCURLOPTTYPE_STRINGPOINT, 16).

/* Set the FTP PORT string (interface name, named or numerical IP address)
 Use i.e '-' to use default address. */
curlopt(xCURLOPT_FTPPORT, xCURLOPTTYPE_STRINGPOINT, 17).

/* Set the User-Agent string (examined by some CGIs) */
curlopt(xCURLOPT_USERAGENT, xCURLOPTTYPE_STRINGPOINT, 18).

/* If the download receives less than "low speed limit" bytes/second
* during "low speed time" seconds, the operations is aborted.
* You could i.e if you have a pretty high speed connection, abort if
* it is less than 2000 bytes/sec during 20 seconds.
*/

/* Set the "low speed limit" */
curlopt(xCURLOPT_LOW_SPEED_LIMIT, xCURLOPTTYPE_LONG, 19).

/* Set the "low speed time" */
curlopt(xCURLOPT_LOW_SPEED_TIME, xCURLOPTTYPE_LONG, 20).

/* Set the continuation offset.
*
* Note there is also a _LARGE version of this key which uses
* off_t types, allowing for large file offsets on platforms which
* use larger-than-32-bit off_t's.  Look below for RESUME_FROM_LARGE.
*/
curlopt(xCURLOPT_RESUME_FROM, xCURLOPTTYPE_LONG, 21).

/* Set cookie in request: */
curlopt(xCURLOPT_COOKIE, xCURLOPTTYPE_STRINGPOINT, 22).

/* This points to a linked list of headers, struct curl_slist kind. This
 list is also used for RTSP (in spite of its name) */
curlopt(xCURLOPT_HTTPHEADER, xCURLOPTTYPE_SLISTPOINT, 23).

/* This points to a linked list of post entries, struct curl_httppost */
curlopt(xCURLOPT_HTTPPOST, xCURLOPTTYPE_OBJECTPOINT, 24).

/* name of the file keeping your private SSL-certificate */
curlopt(xCURLOPT_SSLCERT, xCURLOPTTYPE_STRINGPOINT, 25).

/* password for the SSL or SSH private key */
curlopt(xCURLOPT_KEYPASSWD, xCURLOPTTYPE_STRINGPOINT, 26).

/* send TYPE parameter? */
curlopt(xCURLOPT_CRLF, xCURLOPTTYPE_LONG, 27).

/* send linked-list of QUOTE commands */
curlopt(xCURLOPT_QUOTE, xCURLOPTTYPE_SLISTPOINT, 28).

/* send FILE * or void * to store headers to, if you use a callback it
 is simply passed to the callback unmodified */
curlopt(xCURLOPT_HEADERDATA, xCURLOPTTYPE_CBPOINT, 29).

/* point to a file to read the initial cookies from, also enables
 "cookie awareness" */
curlopt(xCURLOPT_COOKIEFILE, xCURLOPTTYPE_STRINGPOINT, 31).

/* What version to specifically try to use.
 See CURL_SSLVERSION defines below. */
curlopt(xCURLOPT_SSLVERSION, xCURLOPTTYPE_VALUES, 32).

/* What kind of HTTP time condition to use, see defines */
curlopt(xCURLOPT_TIMECONDITION, xCURLOPTTYPE_VALUES, 33).

/* Time to use with the above condition. Specified in number of seconds
 since 1 Jan 1970 */
curlopt(xCURLOPT_TIMEVALUE, xCURLOPTTYPE_LONG, 34).

/* 35 = OBSOLETE */

/* Custom request, for customizing the get command like
 HTTP: DELETE, TRACE and others
 FTP: to use a different list command
 */
curlopt(xCURLOPT_CUSTOMREQUEST, xCURLOPTTYPE_STRINGPOINT, 36).

/* FILE handle to use instead of stderr */
curlopt(xCURLOPT_STDERR, xCURLOPTTYPE_OBJECTPOINT, 37).

/* 38 is not used */

/* send linked-list of post-transfer QUOTE commands */
curlopt(xCURLOPT_POSTQUOTE, xCURLOPTTYPE_SLISTPOINT, 39).

/* OBSOLETE, do not use! */
curlopt(xCURLOPT_OBSOLETE40, xCURLOPTTYPE_OBJECTPOINT, 40).

/* talk a lot */
curlopt(xCURLOPT_VERBOSE, xCURLOPTTYPE_LONG, 41).

/* throw the header out too */
curlopt(xCURLOPT_HEADER, xCURLOPTTYPE_LONG, 42).

/* shut off the progress meter */
curlopt(xCURLOPT_NOPROGRESS, xCURLOPTTYPE_LONG, 43).

/* use HEAD to get http document */
curlopt(xCURLOPT_NOBODY, xCURLOPTTYPE_LONG, 44).

/* no output on http error codes >= 400 */
curlopt(xCURLOPT_FAILONERROR, xCURLOPTTYPE_LONG, 45).

/* this is an upload */
curlopt(xCURLOPT_UPLOAD, xCURLOPTTYPE_LONG, 46).

/* HTTP POST method */
curlopt(xCURLOPT_POST, xCURLOPTTYPE_LONG, 47).

/* bare names when listing directories */
curlopt(xCURLOPT_DIRLISTONLY, xCURLOPTTYPE_LONG, 48).

/* Append instead of overwrite on upload! */
curlopt(xCURLOPT_APPEND, xCURLOPTTYPE_LONG, 50).

/* Specify whether to read the user+password from the .netrc or the URL.
* This must be one of the CURL_NETRC_* enums below. */
curlopt(xCURLOPT_NETRC, xCURLOPTTYPE_VALUES, 51).

/* use Location: Luke! */
curlopt(xCURLOPT_FOLLOWLOCATION, xCURLOPTTYPE_LONG, 52).

/* transfer data in text/ASCII format */
curlopt(xCURLOPT_TRANSFERTEXT, xCURLOPTTYPE_LONG, 53).

/* HTTP PUT */
curlopt(xCURLOPT_PUT, xCURLOPTTYPE_LONG, 54).

/* 55 = OBSOLETE */

/* DEPRECATED
* Function that will be called instead of the internal progress display
* function. This function should be defined as the curl_progress_callback
* prototype defines. */
curlopt(xCURLOPT_PROGRESSFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 56).

/* Data passed to the xCURLOPT_PROGRESSFUNCTION and xCURLOPT_XFERINFOFUNCTION
 callbacks */
curlopt(xCURLOPT_XFERINFODATA, xCURLOPTTYPE_CBPOINT, 57).
curlopt(xCURLOPT_PROGRESSDATA, xCURLOPTTYPE_CBPOINT, 57).

/* We want the referrer field set automatically when following locations */
curlopt(xCURLOPT_AUTOREFERER, xCURLOPTTYPE_LONG, 58).

/* Port of the proxy, can be set in the proxy string as well with:
 "[host]:[port]" */
curlopt(xCURLOPT_PROXYPORT, xCURLOPTTYPE_LONG, 59).

/* size of the POST input data, if strlen() is not good to use */
curlopt(xCURLOPT_POSTFIELDSIZE, xCURLOPTTYPE_LONG, 60).

/* tunnel non-http operations through a HTTP proxy */
curlopt(xCURLOPT_HTTPPROXYTUNNEL, xCURLOPTTYPE_LONG, 61).

/* Set the interface string to use as outgoing network interface */
curlopt(xCURLOPT_INTERFACE, xCURLOPTTYPE_STRINGPOINT, 62).

/* Set the krb4/5 security level, this also enables krb4/5 awareness.  This
* is a string, 'clear', 'safe', 'confidential' or 'private'.  If the string
* is set but doesn't match one of these, 'private' will be used.  */
curlopt(xCURLOPT_KRBLEVEL, xCURLOPTTYPE_STRINGPOINT, 63).

/* Set if we should verify the peer in ssl handshake, set 1 to verify. */
curlopt(xCURLOPT_SSL_VERIFYPEER, xCURLOPTTYPE_LONG, 64).

/* The CApath or CAfile used to validate the peer certificate
 this option is used only if SSL_VERIFYPEER is true */
curlopt(xCURLOPT_CAINFO, xCURLOPTTYPE_STRINGPOINT, 65).

/* 66 = OBSOLETE */
/* 67 = OBSOLETE */

/* Maximum number of http redirects to follow */
curlopt(xCURLOPT_MAXREDIRS, xCURLOPTTYPE_LONG, 68).

/* Pass a long set to 1 to get the date of the requested document (if
 possible)! Pass a zero to shut it off. */
curlopt(xCURLOPT_FILETIME, xCURLOPTTYPE_LONG, 69).

/* This points to a linked list of telnet options */
curlopt(xCURLOPT_TELNETOPTIONS, xCURLOPTTYPE_SLISTPOINT, 70).

/* Max amount of cached alive connections */
curlopt(xCURLOPT_MAXCONNECTS, xCURLOPTTYPE_LONG, 71).

/* OBSOLETE, do not use! */
curlopt(xCURLOPT_OBSOLETE72, xCURLOPTTYPE_LONG, 72).

/* 73 = OBSOLETE */

/* Set to explicitly use a new connection for the upcoming transfer.
 Do not use this unless you're absolutely sure of this, as it makes the
 operation slower and is less friendly for the network. */
curlopt(xCURLOPT_FRESH_CONNECT, xCURLOPTTYPE_LONG, 74).

/* Set to explicitly forbid the upcoming transfer's connection to be re-used
 when done. Do not use this unless you're absolutely sure of this, as it
 makes the operation slower and is less friendly for the network. */
curlopt(xCURLOPT_FORBID_REUSE, xCURLOPTTYPE_LONG, 75).

/* Set to a file name that contains random data for libcurl to use to
 seed the random engine when doing SSL connects. */
curlopt(xCURLOPT_RANDOM_FILE, xCURLOPTTYPE_STRINGPOINT, 76).

/* Set to the Entropy Gathering Daemon socket pathname */
curlopt(xCURLOPT_EGDSOCKET, xCURLOPTTYPE_STRINGPOINT, 77).

/* Time-out connect operations after this amount of seconds, if connects are
 OK within this time, then fine... This only aborts the connect phase. */
curlopt(xCURLOPT_CONNECTTIMEOUT, xCURLOPTTYPE_LONG, 78).

/* Function that will be called to store headers (instead of fwrite). The
* parameters will use fwrite() syntax, make sure to follow them. */
curlopt(xCURLOPT_HEADERFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 79).

/* Set this to force the HTTP request to get back to GET. Only really usable
 if POST, PUT or a custom request have been used first.
*/
curlopt(xCURLOPT_HTTPGET, xCURLOPTTYPE_LONG, 80).

/* Set if we should verify the Common name from the peer certificate in ssl
* handshake, set 1 to check existence, 2 to ensure that it matches the
* provided hostname. */
curlopt(xCURLOPT_SSL_VERIFYHOST, xCURLOPTTYPE_LONG, 81).

/* Specify which file name to write all known cookies in after completed
 operation. Set file name to "-" (dash) to make it go to stdout. */
curlopt(xCURLOPT_COOKIEJAR, xCURLOPTTYPE_STRINGPOINT, 82).

/* Specify which SSL ciphers to use */
curlopt(xCURLOPT_SSL_CIPHER_LIST, xCURLOPTTYPE_STRINGPOINT, 83).

/* Specify which HTTP version to use! This must be set to one of the
 CURL_HTTP_VERSION* enums set below. */
curlopt(xCURLOPT_HTTP_VERSION, xCURLOPTTYPE_VALUES, 84).

/* Specifically switch on or off the FTP engine's use of the EPSV command. By
 default, that one will always be attempted before the more traditional
 PASV command. */
curlopt(xCURLOPT_FTP_USE_EPSV, xCURLOPTTYPE_LONG, 85).

/* type of the file keeping your SSL-certificate ("DER", "PEM", "ENG") */
curlopt(xCURLOPT_SSLCERTTYPE, xCURLOPTTYPE_STRINGPOINT, 86).

/* name of the file keeping your private SSL-key */
curlopt(xCURLOPT_SSLKEY, xCURLOPTTYPE_STRINGPOINT, 87).

/* type of the file keeping your private SSL-key ("DER", "PEM", "ENG") */
curlopt(xCURLOPT_SSLKEYTYPE, xCURLOPTTYPE_STRINGPOINT, 88).

/* crypto engine for the SSL-sub system */
curlopt(xCURLOPT_SSLENGINE, xCURLOPTTYPE_STRINGPOINT, 89).

/* set the crypto engine for the SSL-sub system as default
 the param has no meaning...
*/
curlopt(xCURLOPT_SSLENGINE_DEFAULT, xCURLOPTTYPE_LONG, 90).

/* Non-zero value means to use the global dns cache */
/* DEPRECATED, do not use! */
curlopt(xCURLOPT_DNS_USE_GLOBAL_CACHE, xCURLOPTTYPE_LONG, 91).

/* DNS cache timeout */
curlopt(xCURLOPT_DNS_CACHE_TIMEOUT, xCURLOPTTYPE_LONG, 92).

/* send linked-list of pre-transfer QUOTE commands */
curlopt(xCURLOPT_PREQUOTE, xCURLOPTTYPE_SLISTPOINT, 93).

/* set the debug function */
curlopt(xCURLOPT_DEBUGFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 94).

/* set the data for the debug function */
curlopt(xCURLOPT_DEBUGDATA, xCURLOPTTYPE_CBPOINT, 95).

/* mark this as start of a cookie session */
curlopt(xCURLOPT_COOKIESESSION, xCURLOPTTYPE_LONG, 96).

/* The CApath directory used to validate the peer certificate
 this option is used only if SSL_VERIFYPEER is true */
curlopt(xCURLOPT_CAPATH, xCURLOPTTYPE_STRINGPOINT, 97).

/* Instruct libcurl to use a smaller receive buffer */
curlopt(xCURLOPT_BUFFERSIZE, xCURLOPTTYPE_LONG, 98).

/* Instruct libcurl to not use any signal/alarm handlers, even when using
 timeouts. This option is useful for multi-threaded applications.
 See libcurl-the-guide for more background information. */
curlopt(xCURLOPT_NOSIGNAL, xCURLOPTTYPE_LONG, 99).

/* Provide a CURLShare for mutexing non-ts data */
curlopt(xCURLOPT_SHARE, xCURLOPTTYPE_OBJECTPOINT, 100).

/* indicates type of proxy. accepted values are CURLPROXY_HTTP (default).
 CURLPROXY_HTTPS, CURLPROXY_SOCKS4, CURLPROXY_SOCKS4A and
 CURLPROXY_SOCKS5. */
curlopt(xCURLOPT_PROXYTYPE, xCURLOPTTYPE_VALUES, 101).

/* Set the Accept-Encoding string. Use this to tell a server you would like
 the response to be compressed. Before 7.21.6, this was known as
 xCURLOPT_ENCODING */
curlopt(xCURLOPT_ACCEPT_ENCODING, xCURLOPTTYPE_STRINGPOINT, 102).

/* Set pointer to private data */
curlopt(xCURLOPT_PRIVATE, xCURLOPTTYPE_OBJECTPOINT, 103).

/* Set aliases for HTTP 200 in the HTTP Response header */
curlopt(xCURLOPT_HTTP200ALIASES, xCURLOPTTYPE_SLISTPOINT, 104).

/* Continue to send authentication (user+password) when following locations,
 even when hostname changed. This can potentially send off the name
 and password to whatever host the server decides. */
curlopt(xCURLOPT_UNRESTRICTED_AUTH, xCURLOPTTYPE_LONG, 105).

/* Specifically switch on or off the FTP engine's use of the EPRT command (
 it also disables the LPRT attempt). By default, those ones will always be
 attempted before the good old traditional PORT command. */
curlopt(xCURLOPT_FTP_USE_EPRT, xCURLOPTTYPE_LONG, 106).

/* Set this to a bitmask value to enable the particular authentications
 methods you like. Use this in combination with xCURLOPT_USERPWD.
 Note that setting multiple bits may cause extra network round-trips. */
curlopt(xCURLOPT_HTTPAUTH, xCURLOPTTYPE_VALUES, 107).

/* Set the ssl context callback function, currently only for OpenSSL or
 WolfSSL ssl_ctx, or mbedTLS mbedtls_ssl_config in the second argument.
 The function must match the curl_ssl_ctx_callback prototype. */
curlopt(xCURLOPT_SSL_CTX_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 108).

/* Set the userdata for the ssl context callback function's third
 argument */
curlopt(xCURLOPT_SSL_CTX_DATA, xCURLOPTTYPE_CBPOINT, 109).

/* FTP Option that causes missing dirs to be created on the remote server.
 In 7.19.4 we introduced the convenience enums for this option using the
 CURLFTP_CREATE_DIR prefix.
*/
curlopt(xCURLOPT_FTP_CREATE_MISSING_DIRS, xCURLOPTTYPE_LONG, 110).

/* Set this to a bitmask value to enable the particular authentications
 methods you like. Use this in combination with xCURLOPT_PROXYUSERPWD.
 Note that setting multiple bits may cause extra network round-trips. */
curlopt(xCURLOPT_PROXYAUTH, xCURLOPTTYPE_VALUES, 111).

/* Option that changes the timeout, in seconds, associated with getting a
 response.  This is different from transfer timeout time and essentially
 places a demand on the server to acknowledge commands in a timely
 manner. For FTP, SMTP, IMAP and POP3. */
curlopt(xCURLOPT_SERVER_RESPONSE_TIMEOUT, xCURLOPTTYPE_LONG, 112).

/* Set this option to one of the CURL_IPRESOLVE_* defines (see below) to
 tell libcurl to use those IP versions only. This only has effect on
 systems with support for more than one, i.e IPv4 _and_ IPv6. */
curlopt(xCURLOPT_IPRESOLVE, xCURLOPTTYPE_VALUES, 113).

/* Set this option to limit the size of a file that will be downloaded from
 an HTTP or FTP server.

 Note there is also _LARGE version which adds large file support for
 platforms which have larger off_t sizes.  See MAXFILESIZE_LARGE below. */
curlopt(xCURLOPT_MAXFILESIZE, xCURLOPTTYPE_LONG, 114).

/* See the comment for INFILESIZE above, but in short, specifies
* the size of the file being uploaded.  -1 means unknown.
*/
curlopt(xCURLOPT_INFILESIZE_LARGE, xCURLOPTTYPE_OFF_T, 115).

/* Sets the continuation offset.  There is also a CURLOPTTYPE_LONG version
* of this; look above for RESUME_FROM.
*/
curlopt(xCURLOPT_RESUME_FROM_LARGE, xCURLOPTTYPE_OFF_T, 116).

/* Sets the maximum size of data that will be downloaded from
* an HTTP or FTP server.  See MAXFILESIZE above for the LONG version.
*/
curlopt(xCURLOPT_MAXFILESIZE_LARGE, xCURLOPTTYPE_OFF_T, 117).

/* Set this option to the file name of your .netrc file you want libcurl
 to parse (using the xCURLOPT_NETRC option). If not set, libcurl will do
 a poor attempt to find the user's home directory and check for a .netrc
 file in there. */
curlopt(xCURLOPT_NETRC_FILE, xCURLOPTTYPE_STRINGPOINT, 118).

/* Enable SSL/TLS for FTP, pick one of:
 CURLUSESSL_TRY     - try using SSL, proceed anyway otherwise
 CURLUSESSL_CONTROL - SSL for the control connection or fail
 CURLUSESSL_ALL     - SSL for all communication or fail
*/
curlopt(xCURLOPT_USE_SSL, xCURLOPTTYPE_VALUES, 119).

/* The _LARGE version of the standard POSTFIELDSIZE option */
curlopt(xCURLOPT_POSTFIELDSIZE_LARGE, xCURLOPTTYPE_OFF_T, 120).

/* Enable/disable the TCP Nagle algorithm */
curlopt(xCURLOPT_TCP_NODELAY, xCURLOPTTYPE_LONG, 121).

/* 122 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */
/* 123 OBSOLETE. Gone in 7.16.0 */
/* 124 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */
/* 125 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */
/* 126 OBSOLETE, used in 7.12.3. Gone in 7.13.0 */
/* 127 OBSOLETE. Gone in 7.16.0 */
/* 128 OBSOLETE. Gone in 7.16.0 */

/* When FTP over SSL/TLS is selected (with xCURLOPT_USE_SSL). this option
 can be used to change libcurl's default action which is to first try
 "AUTH SSL" and then "AUTH TLS" in this order, and proceed when a OK
 response has been received.

 Available parameters are:
 CURLFTPAUTH_DEFAULT - let libcurl decide
 CURLFTPAUTH_SSL     - try "AUTH SSL" first, then TLS
 CURLFTPAUTH_TLS     - try "AUTH TLS" first, then SSL
*/
curlopt(xCURLOPT_FTPSSLAUTH, xCURLOPTTYPE_VALUES, 129).

curlopt(xCURLOPT_IOCTLFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 130).
curlopt(xCURLOPT_IOCTLDATA, xCURLOPTTYPE_CBPOINT, 131).

/* 132 OBSOLETE. Gone in 7.16.0 */
/* 133 OBSOLETE. Gone in 7.16.0 */

/* null-terminated string for pass on to the FTP server when asked for
 "account" info */
curlopt(xCURLOPT_FTP_ACCOUNT, xCURLOPTTYPE_STRINGPOINT, 134).

/* feed cookie into cookie engine */
curlopt(xCURLOPT_COOKIELIST, xCURLOPTTYPE_STRINGPOINT, 135).

/* ignore Content-Length */
curlopt(xCURLOPT_IGNORE_CONTENT_LENGTH, xCURLOPTTYPE_LONG, 136).

/* Set to non-zero to skip the IP address received in a 227 PASV FTP server
 response. Typically used for FTP-SSL purposes but is not restricted to
 that. libcurl will then instead use the same IP address it used for the
 control connection. */
curlopt(xCURLOPT_FTP_SKIP_PASV_IP, xCURLOPTTYPE_LONG, 137).

/* Select "file method" to use when doing FTP, see the curl_ftpmethod
 above. */
curlopt(xCURLOPT_FTP_FILEMETHOD, xCURLOPTTYPE_VALUES, 138).

/* Local port number to bind the socket to */
curlopt(xCURLOPT_LOCALPORT, xCURLOPTTYPE_LONG, 139).

/* Number of ports to try, including the first one set with LOCALPORT.
 Thus, setting it to 1 will make no additional attempts but the first.
*/
curlopt(xCURLOPT_LOCALPORTRANGE, xCURLOPTTYPE_LONG, 140).

/* no transfer, set up connection and let application use the socket by
 extracting it with CURLINFO_LASTSOCKET */
curlopt(xCURLOPT_CONNECT_ONLY, xCURLOPTTYPE_LONG, 141).

/* Function that will be called to convert from the
 network encoding (instead of using the iconv calls in libcurl) */
curlopt(xCURLOPT_CONV_FROM_NETWORK_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 142).

/* Function that will be called to convert to the
 network encoding (instead of using the iconv calls in libcurl) */
curlopt(xCURLOPT_CONV_TO_NETWORK_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 143).

/* Function that will be called to convert from UTF8
 (instead of using the iconv calls in libcurl)
 Note that this is used only for SSL certificate processing */
curlopt(xCURLOPT_CONV_FROM_UTF8_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 144).

/* if the connection proceeds too quickly then need to slow it down */
/* limit-rate: maximum number of bytes per second to send or receive */
curlopt(xCURLOPT_MAX_SEND_SPEED_LARGE, xCURLOPTTYPE_OFF_T, 145).
curlopt(xCURLOPT_MAX_RECV_SPEED_LARGE, xCURLOPTTYPE_OFF_T, 146).

/* Pointer to command string to send if USER/PASS fails. */
curlopt(xCURLOPT_FTP_ALTERNATIVE_TO_USER, xCURLOPTTYPE_STRINGPOINT, 147).

/* callback function for setting socket options */
curlopt(xCURLOPT_SOCKOPTFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 148).
curlopt(xCURLOPT_SOCKOPTDATA, xCURLOPTTYPE_CBPOINT, 149).

/* set to 0 to disable session ID re-use for this transfer, default is
 enabled (== 1) */
curlopt(xCURLOPT_SSL_SESSIONID_CACHE, xCURLOPTTYPE_LONG, 150).

/* allowed SSH authentication methods */
curlopt(xCURLOPT_SSH_AUTH_TYPES, xCURLOPTTYPE_VALUES, 151).

/* Used by scp/sftp to do public/private key authentication */
curlopt(xCURLOPT_SSH_PUBLIC_KEYFILE, xCURLOPTTYPE_STRINGPOINT, 152).
curlopt(xCURLOPT_SSH_PRIVATE_KEYFILE, xCURLOPTTYPE_STRINGPOINT, 153).

/* Send CCC (Clear Command Channel) after authentication */
curlopt(xCURLOPT_FTP_SSL_CCC, xCURLOPTTYPE_LONG, 154).

/* Same as TIMEOUT and CONNECTTIMEOUT, but with ms resolution */
curlopt(xCURLOPT_TIMEOUT_MS, xCURLOPTTYPE_LONG, 155).
curlopt(xCURLOPT_CONNECTTIMEOUT_MS, xCURLOPTTYPE_LONG, 156).

/* set to zero to disable the libcurl's decoding and thus pass the raw body
 data to the application even when it is encoded/compressed */
curlopt(xCURLOPT_HTTP_TRANSFER_DECODING, xCURLOPTTYPE_LONG, 157).
curlopt(xCURLOPT_HTTP_CONTENT_DECODING, xCURLOPTTYPE_LONG, 158).

/* Permission used when creating new files and directories on the remote
 server for protocols that support it, SFTP/SCP/FILE */
curlopt(xCURLOPT_NEW_FILE_PERMS, xCURLOPTTYPE_LONG, 159).
curlopt(xCURLOPT_NEW_DIRECTORY_PERMS, xCURLOPTTYPE_LONG, 160).

/* Set the behavior of POST when redirecting. Values must be set to one
 of CURL_REDIR* defines below. This used to be called xCURLOPT_POST301 */
curlopt(xCURLOPT_POSTREDIR, xCURLOPTTYPE_VALUES, 161).

/* used by scp/sftp to verify the host's public key */
curlopt(xCURLOPT_SSH_HOST_PUBLIC_KEY_MD5, xCURLOPTTYPE_STRINGPOINT, 162).

/* Callback function for opening socket (instead of socket(2)). Optionally,
 callback is able change the address or refuse to connect returning
 CURL_SOCKET_BAD.  The callback should have type
 curl_opensocket_callback */
curlopt(xCURLOPT_OPENSOCKETFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 163).
curlopt(xCURLOPT_OPENSOCKETDATA, xCURLOPTTYPE_CBPOINT, 164).

/* POST volatile input fields. */
curlopt(xCURLOPT_COPYPOSTFIELDS, xCURLOPTTYPE_OBJECTPOINT, 165).

/* set transfer mode (;type=<a|i>) when doing FTP via an HTTP proxy */
curlopt(xCURLOPT_PROXY_TRANSFER_MODE, xCURLOPTTYPE_LONG, 166).

/* Callback function for seeking in the input stream */
curlopt(xCURLOPT_SEEKFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 167).
curlopt(xCURLOPT_SEEKDATA, xCURLOPTTYPE_CBPOINT, 168).

/* CRL file */
curlopt(xCURLOPT_CRLFILE, xCURLOPTTYPE_STRINGPOINT, 169).

/* Issuer certificate */
curlopt(xCURLOPT_ISSUERCERT, xCURLOPTTYPE_STRINGPOINT, 170).

/* (IPv6) Address scope */
curlopt(xCURLOPT_ADDRESS_SCOPE, xCURLOPTTYPE_LONG, 171).

/* Collect certificate chain info and allow it to get retrievable with
 CURLINFO_CERTINFO after the transfer is complete. */
curlopt(xCURLOPT_CERTINFO, xCURLOPTTYPE_LONG, 172).

/* "name" and "pwd" to use when fetching. */
curlopt(xCURLOPT_USERNAME, xCURLOPTTYPE_STRINGPOINT, 173).
curlopt(xCURLOPT_PASSWORD, xCURLOPTTYPE_STRINGPOINT, 174).

/* "name" and "pwd" to use with Proxy when fetching. */
curlopt(xCURLOPT_PROXYUSERNAME, xCURLOPTTYPE_STRINGPOINT, 175).
curlopt(xCURLOPT_PROXYPASSWORD, xCURLOPTTYPE_STRINGPOINT, 176).

/* Comma separated list of hostnames defining no-proxy zones. These should
 match both hostnames directly, and hostnames within a domain. For
 example, local.com will match local.com and www.local.com, but NOT
 notlocal.com or www.notlocal.com. For compatibility with other
 implementations of this, .local.com will be considered to be the same as
 local.com. A single * is the only valid wildcard, and effectively
 disables the use of proxy. */
curlopt(xCURLOPT_NOPROXY, xCURLOPTTYPE_STRINGPOINT, 177).

/* block size for TFTP transfers */
curlopt(xCURLOPT_TFTP_BLKSIZE, xCURLOPTTYPE_LONG, 178).

/* Socks Service */
/* DEPRECATED, do not use! */
curlopt(xCURLOPT_SOCKS5_GSSAPI_SERVICE, xCURLOPTTYPE_STRINGPOINT, 179).

/* Socks Service */
curlopt(xCURLOPT_SOCKS5_GSSAPI_NEC, xCURLOPTTYPE_LONG, 180).

/* set the bitmask for the protocols that are allowed to be used for the
 transfer, which thus helps the app which takes URLs from users or other
 external inputs and want to restrict what protocol(s) to deal
 with. Defaults to CURLPROTO_ALL. */
curlopt(xCURLOPT_PROTOCOLS, xCURLOPTTYPE_LONG, 181).

/* set the bitmask for the protocols that libcurl is allowed to follow to,
 as a subset of the xCURLOPT_PROTOCOLS ones. That means the protocol needs
 to be set in both bitmasks to be allowed to get redirected to. */
curlopt(xCURLOPT_REDIR_PROTOCOLS, xCURLOPTTYPE_LONG, 182).

/* set the SSH knownhost file name to use */
curlopt(xCURLOPT_SSH_KNOWNHOSTS, xCURLOPTTYPE_STRINGPOINT, 183).

/* set the SSH host key callback, must point to a curl_sshkeycallback
 function */
curlopt(xCURLOPT_SSH_KEYFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 184).

/* set the SSH host key callback custom pointer */
curlopt(xCURLOPT_SSH_KEYDATA, xCURLOPTTYPE_CBPOINT, 185).

/* set the SMTP mail originator */
curlopt(xCURLOPT_MAIL_FROM, xCURLOPTTYPE_STRINGPOINT, 186).

/* set the list of SMTP mail receiver(s) */
curlopt(xCURLOPT_MAIL_RCPT, xCURLOPTTYPE_SLISTPOINT, 187).

/* FTP: send PRET before PASV */
curlopt(xCURLOPT_FTP_USE_PRET, xCURLOPTTYPE_LONG, 188).

/* RTSP request method (OPTIONS, SETUP, PLAY, etc...) */
curlopt(xCURLOPT_RTSP_REQUEST, xCURLOPTTYPE_VALUES, 189).

/* The RTSP session identifier */
curlopt(xCURLOPT_RTSP_SESSION_ID, xCURLOPTTYPE_STRINGPOINT, 190).

/* The RTSP stream URI */
curlopt(xCURLOPT_RTSP_STREAM_URI, xCURLOPTTYPE_STRINGPOINT, 191).

/* The Transport: header to use in RTSP requests */
curlopt(xCURLOPT_RTSP_TRANSPORT, xCURLOPTTYPE_STRINGPOINT, 192).

/* Manually initialize the client RTSP CSeq for this handle */
curlopt(xCURLOPT_RTSP_CLIENT_CSEQ, xCURLOPTTYPE_LONG, 193).

/* Manually initialize the server RTSP CSeq for this handle */
curlopt(xCURLOPT_RTSP_SERVER_CSEQ, xCURLOPTTYPE_LONG, 194).

/* The stream to pass to INTERLEAVEFUNCTION. */
curlopt(xCURLOPT_INTERLEAVEDATA, xCURLOPTTYPE_CBPOINT, 195).

/* Let the application define a custom write method for RTP data */
curlopt(xCURLOPT_INTERLEAVEFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 196).

/* Turn on wildcard matching */
curlopt(xCURLOPT_WILDCARDMATCH, xCURLOPTTYPE_LONG, 197).

/* Directory matching callback called before downloading of an
 individual file (chunk) started */
curlopt(xCURLOPT_CHUNK_BGN_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 198).

/* Directory matching callback called after the file (chunk)
 was downloaded, or skipped */
curlopt(xCURLOPT_CHUNK_END_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 199).

/* Change match (fnmatch-like) callback for wildcard matching */
curlopt(xCURLOPT_FNMATCH_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 200).

/* Let the application define custom chunk data pointer */
curlopt(xCURLOPT_CHUNK_DATA, xCURLOPTTYPE_CBPOINT, 201).

/* FNMATCH_FUNCTION user pointer */
curlopt(xCURLOPT_FNMATCH_DATA, xCURLOPTTYPE_CBPOINT, 202).

/* send linked-list of name:port:address sets */
curlopt(xCURLOPT_RESOLVE, xCURLOPTTYPE_SLISTPOINT, 203).

/* Set a username for authenticated TLS */
curlopt(xCURLOPT_TLSAUTH_USERNAME, xCURLOPTTYPE_STRINGPOINT, 204).

/* Set a password for authenticated TLS */
curlopt(xCURLOPT_TLSAUTH_PASSWORD, xCURLOPTTYPE_STRINGPOINT, 205).

/* Set authentication type for authenticated TLS */
curlopt(xCURLOPT_TLSAUTH_TYPE, xCURLOPTTYPE_STRINGPOINT, 206).

/* Set to 1 to enable the "TE:" header in HTTP requests to ask for
 compressed transfer-encoded responses. Set to 0 to disable the use of TE:
 in outgoing requests. The current default is 0, but it might change in a
 future libcurl release.

 libcurl will ask for the compressed methods it knows of, and if that
 isn't any, it will not ask for transfer-encoding at all even if this
 option is set to 1.

*/
curlopt(xCURLOPT_TRANSFER_ENCODING, xCURLOPTTYPE_LONG, 207).

/* Callback function for closing socket (instead of close(2)). The callback
 should have type curl_closesocket_callback */
curlopt(xCURLOPT_CLOSESOCKETFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 208).
curlopt(xCURLOPT_CLOSESOCKETDATA, xCURLOPTTYPE_CBPOINT, 209).

/* allow GSSAPI credential delegation */
curlopt(xCURLOPT_GSSAPI_DELEGATION, xCURLOPTTYPE_VALUES, 210).

/* Set the name servers to use for DNS resolution */
curlopt(xCURLOPT_DNS_SERVERS, xCURLOPTTYPE_STRINGPOINT, 211).

/* Time-out accept operations (currently for FTP only) after this amount
 of milliseconds. */
curlopt(xCURLOPT_ACCEPTTIMEOUT_MS, xCURLOPTTYPE_LONG, 212).

/* Set TCP keepalive */
curlopt(xCURLOPT_TCP_KEEPALIVE, xCURLOPTTYPE_LONG, 213).

/* non-universal keepalive knobs (Linux, AIX, HP-UX, more) */
curlopt(xCURLOPT_TCP_KEEPIDLE, xCURLOPTTYPE_LONG, 214).
curlopt(xCURLOPT_TCP_KEEPINTVL, xCURLOPTTYPE_LONG, 215).

/* Enable/disable specific SSL features with a bitmask, see CURLSSLOPT_* */
curlopt(xCURLOPT_SSL_OPTIONS, xCURLOPTTYPE_VALUES, 216).

/* Set the SMTP auth originator */
curlopt(xCURLOPT_MAIL_AUTH, xCURLOPTTYPE_STRINGPOINT, 217).

/* Enable/disable SASL initial response */
curlopt(xCURLOPT_SASL_IR, xCURLOPTTYPE_LONG, 218).

/* Function that will be called instead of the internal progress display
* function. This function should be defined as the curl_xferinfo_callback
* prototype defines. (Deprecates xCURLOPT_PROGRESSFUNCTION) */
curlopt(xCURLOPT_XFERINFOFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 219).

/* The XOAUTH2 bearer token */
curlopt(xCURLOPT_XOAUTH2_BEARER, xCURLOPTTYPE_STRINGPOINT, 220).

/* Set the interface string to use as outgoing network
* interface for DNS requests.
* Only supported by the c-ares DNS backend */
curlopt(xCURLOPT_DNS_INTERFACE, xCURLOPTTYPE_STRINGPOINT, 221).

/* Set the local IPv4 address to use for outgoing DNS requests.
* Only supported by the c-ares DNS backend */
curlopt(xCURLOPT_DNS_LOCAL_IP4, xCURLOPTTYPE_STRINGPOINT, 222).

/* Set the local IPv6 address to use for outgoing DNS requests.
* Only supported by the c-ares DNS backend */
curlopt(xCURLOPT_DNS_LOCAL_IP6, xCURLOPTTYPE_STRINGPOINT, 223).

/* Set authentication options directly */
curlopt(xCURLOPT_LOGIN_OPTIONS, xCURLOPTTYPE_STRINGPOINT, 224).

/* Enable/disable TLS NPN extension (http2 over ssl might fail without) */
curlopt(xCURLOPT_SSL_ENABLE_NPN, xCURLOPTTYPE_LONG, 225).

/* Enable/disable TLS ALPN extension (http2 over ssl might fail without) */
curlopt(xCURLOPT_SSL_ENABLE_ALPN, xCURLOPTTYPE_LONG, 226).

/* Time to wait for a response to a HTTP request containing an
* Expect: 100-continue header before sending the data anyway. */
curlopt(xCURLOPT_EXPECT_100_TIMEOUT_MS, xCURLOPTTYPE_LONG, 227).

/* This points to a linked list of headers used for proxy requests only,
 struct curl_slist kind */
curlopt(xCURLOPT_PROXYHEADER, xCURLOPTTYPE_SLISTPOINT, 228).

/* Pass in a bitmask of "header options" */
curlopt(xCURLOPT_HEADEROPT, xCURLOPTTYPE_VALUES, 229).

/* The public key in DER form used to validate the peer public key
 this option is used only if SSL_VERIFYPEER is true */
curlopt(xCURLOPT_PINNEDPUBLICKEY, xCURLOPTTYPE_STRINGPOINT, 230).

/* Path to Unix domain socket */
curlopt(xCURLOPT_UNIX_SOCKET_PATH, xCURLOPTTYPE_STRINGPOINT, 231).

/* Set if we should verify the certificate status. */
curlopt(xCURLOPT_SSL_VERIFYSTATUS, xCURLOPTTYPE_LONG, 232).

/* Set if we should enable TLS false start. */
curlopt(xCURLOPT_SSL_FALSESTART, xCURLOPTTYPE_LONG, 233).

/* Do not squash dot-dot sequences */
curlopt(xCURLOPT_PATH_AS_IS, xCURLOPTTYPE_LONG, 234).

/* Proxy Service Name */
curlopt(xCURLOPT_PROXY_SERVICE_NAME, xCURLOPTTYPE_STRINGPOINT, 235).

/* Service Name */
curlopt(xCURLOPT_SERVICE_NAME, xCURLOPTTYPE_STRINGPOINT, 236).

/* Wait/don't wait for pipe/mutex to clarify */
curlopt(xCURLOPT_PIPEWAIT, xCURLOPTTYPE_LONG, 237).

/* Set the protocol used when curl is given a URL without a protocol */
curlopt(xCURLOPT_DEFAULT_PROTOCOL, xCURLOPTTYPE_STRINGPOINT, 238).

/* Set stream weight, 1 - 256 (default is 16) */
curlopt(xCURLOPT_STREAM_WEIGHT, xCURLOPTTYPE_LONG, 239).

/* Set stream dependency on another CURL handle */
curlopt(xCURLOPT_STREAM_DEPENDS, xCURLOPTTYPE_OBJECTPOINT, 240).

/* Set E-xclusive stream dependency on another CURL handle */
curlopt(xCURLOPT_STREAM_DEPENDS_E, xCURLOPTTYPE_OBJECTPOINT, 241).

/* Do not send any tftp option requests to the server */
curlopt(xCURLOPT_TFTP_NO_OPTIONS, xCURLOPTTYPE_LONG, 242).

/* Linked-list of host:port:connect-to-host:connect-to-port,
 overrides the URL's host:port (only for the network layer) */
curlopt(xCURLOPT_CONNECT_TO, xCURLOPTTYPE_SLISTPOINT, 243).

/* Set TCP Fast Open */
curlopt(xCURLOPT_TCP_FASTOPEN, xCURLOPTTYPE_LONG, 244).

/* Continue to send data if the server responds early with an
* HTTP status code >= 300 */
curlopt(xCURLOPT_KEEP_SENDING_ON_ERROR, xCURLOPTTYPE_LONG, 245).

/* The CApath or CAfile used to validate the proxy certificate
 this option is used only if PROXY_SSL_VERIFYPEER is true */
curlopt(xCURLOPT_PROXY_CAINFO, xCURLOPTTYPE_STRINGPOINT, 246).

/* The CApath directory used to validate the proxy certificate
 this option is used only if PROXY_SSL_VERIFYPEER is true */
curlopt(xCURLOPT_PROXY_CAPATH, xCURLOPTTYPE_STRINGPOINT, 247).

/* Set if we should verify the proxy in ssl handshake,
 set 1 to verify. */
curlopt(xCURLOPT_PROXY_SSL_VERIFYPEER, xCURLOPTTYPE_LONG, 248).

/* Set if we should verify the Common name from the proxy certificate in ssl
* handshake, set 1 to check existence, 2 to ensure that it matches
* the provided hostname. */
curlopt(xCURLOPT_PROXY_SSL_VERIFYHOST, xCURLOPTTYPE_LONG, 249).

/* What version to specifically try to use for proxy.
 See CURL_SSLVERSION defines below. */
curlopt(xCURLOPT_PROXY_SSLVERSION, xCURLOPTTYPE_VALUES, 250).

/* Set a username for authenticated TLS for proxy */
curlopt(xCURLOPT_PROXY_TLSAUTH_USERNAME, xCURLOPTTYPE_STRINGPOINT, 251).

/* Set a password for authenticated TLS for proxy */
curlopt(xCURLOPT_PROXY_TLSAUTH_PASSWORD, xCURLOPTTYPE_STRINGPOINT, 252).

/* Set authentication type for authenticated TLS for proxy */
curlopt(xCURLOPT_PROXY_TLSAUTH_TYPE, xCURLOPTTYPE_STRINGPOINT, 253).

/* name of the file keeping your private SSL-certificate for proxy */
curlopt(xCURLOPT_PROXY_SSLCERT, xCURLOPTTYPE_STRINGPOINT, 254).

/* type of the file keeping your SSL-certificate ("DER", "PEM", "ENG") for
 proxy */
curlopt(xCURLOPT_PROXY_SSLCERTTYPE, xCURLOPTTYPE_STRINGPOINT, 255).

/* name of the file keeping your private SSL-key for proxy */
curlopt(xCURLOPT_PROXY_SSLKEY, xCURLOPTTYPE_STRINGPOINT, 256).

/* type of the file keeping your private SSL-key ("DER", "PEM", "ENG") for
 proxy */
curlopt(xCURLOPT_PROXY_SSLKEYTYPE, xCURLOPTTYPE_STRINGPOINT, 257).

/* password for the SSL private key for proxy */
curlopt(xCURLOPT_PROXY_KEYPASSWD, xCURLOPTTYPE_STRINGPOINT, 258).

/* Specify which SSL ciphers to use for proxy */
curlopt(xCURLOPT_PROXY_SSL_CIPHER_LIST, xCURLOPTTYPE_STRINGPOINT, 259).

/* CRL file for proxy */
curlopt(xCURLOPT_PROXY_CRLFILE, xCURLOPTTYPE_STRINGPOINT, 260).

/* Enable/disable specific SSL features with a bitmask for proxy, see
 CURLSSLOPT_* */
curlopt(xCURLOPT_PROXY_SSL_OPTIONS, xCURLOPTTYPE_LONG, 261).

/* Name of pre proxy to use. */
curlopt(xCURLOPT_PRE_PROXY, xCURLOPTTYPE_STRINGPOINT, 262).

/* The public key in DER form used to validate the proxy public key
 this option is used only if PROXY_SSL_VERIFYPEER is true */
curlopt(xCURLOPT_PROXY_PINNEDPUBLICKEY, xCURLOPTTYPE_STRINGPOINT, 263).

/* Path to an abstract Unix domain socket */
curlopt(xCURLOPT_ABSTRACT_UNIX_SOCKET, xCURLOPTTYPE_STRINGPOINT, 264).

/* Suppress proxy CONNECT response headers from user callbacks */
curlopt(xCURLOPT_SUPPRESS_CONNECT_HEADERS, xCURLOPTTYPE_LONG, 265).

/* The request target, instead of extracted from the URL */
curlopt(xCURLOPT_REQUEST_TARGET, xCURLOPTTYPE_STRINGPOINT, 266).

/* bitmask of allowed auth methods for connections to SOCKS5 proxies */
curlopt(xCURLOPT_SOCKS5_AUTH, xCURLOPTTYPE_LONG, 267).

/* Enable/disable SSH compression */
curlopt(xCURLOPT_SSH_COMPRESSION, xCURLOPTTYPE_LONG, 268).

/* Post MIME data. */
curlopt(xCURLOPT_MIMEPOST, xCURLOPTTYPE_OBJECTPOINT, 269).

/* Time to use with the xCURLOPT_TIMECONDITION. Specified in number of
 seconds since 1 Jan 1970. */
curlopt(xCURLOPT_TIMEVALUE_LARGE, xCURLOPTTYPE_OFF_T, 270).

/* Head start in milliseconds to give happy eyeballs. */
curlopt(xCURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS, xCURLOPTTYPE_LONG, 271).

/* Function that will be called before a resolver request is made */
curlopt(xCURLOPT_RESOLVER_START_FUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 272).

/* User data to pass to the resolver start callback. */
curlopt(xCURLOPT_RESOLVER_START_DATA, xCURLOPTTYPE_CBPOINT, 273).

/* send HAProxy PROXY protocol header? */
curlopt(xCURLOPT_HAPROXYPROTOCOL, xCURLOPTTYPE_LONG, 274).

/* shuffle addresses before use when DNS returns multiple */
curlopt(xCURLOPT_DNS_SHUFFLE_ADDRESSES, xCURLOPTTYPE_LONG, 275).

/* Specify which TLS 1.3 ciphers suites to use */
curlopt(xCURLOPT_TLS13_CIPHERS, xCURLOPTTYPE_STRINGPOINT, 276).
curlopt(xCURLOPT_PROXY_TLS13_CIPHERS, xCURLOPTTYPE_STRINGPOINT, 277).

/* Disallow specifying username/login in URL. */
curlopt(xCURLOPT_DISALLOW_USERNAME_IN_URL, xCURLOPTTYPE_LONG, 278).

/* DNS-over-HTTPS URL */
curlopt(xCURLOPT_DOH_URL, xCURLOPTTYPE_STRINGPOINT, 279).

/* Preferred buffer size to use for uploads */
curlopt(xCURLOPT_UPLOAD_BUFFERSIZE, xCURLOPTTYPE_LONG, 280).

/* Time in ms between connection upkeep calls for long-lived connections. */
curlopt(xCURLOPT_UPKEEP_INTERVAL_MS, xCURLOPTTYPE_LONG, 281).

/* Specify URL using CURL URL API. */
curlopt(xCURLOPT_CURLU, xCURLOPTTYPE_OBJECTPOINT, 282).

/* add trailing data just after no more data is available */
curlopt(xCURLOPT_TRAILERFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 283).

/* pointer to be passed to HTTP_TRAILER_FUNCTION */
curlopt(xCURLOPT_TRAILERDATA, xCURLOPTTYPE_CBPOINT, 284).

/* set this to 1L to allow HTTP/0.9 responses or 0L to disallow */
curlopt(xCURLOPT_HTTP09_ALLOWED, xCURLOPTTYPE_LONG, 285).

/* alt-svc control bitmask */
curlopt(xCURLOPT_ALTSVC_CTRL, xCURLOPTTYPE_LONG, 286).

/* alt-svc cache file name to possibly read from/write to */
curlopt(xCURLOPT_ALTSVC, xCURLOPTTYPE_STRINGPOINT, 287).

/* maximum age (idle time) of a connection to consider it for reuse
* (in seconds) */
curlopt(xCURLOPT_MAXAGE_CONN, xCURLOPTTYPE_LONG, 288).

/* SASL authorization identity */
curlopt(xCURLOPT_SASL_AUTHZID, xCURLOPTTYPE_STRINGPOINT, 289).

/* allow RCPT TO command to fail for some recipients */
curlopt(xCURLOPT_MAIL_RCPT_ALLLOWFAILS, xCURLOPTTYPE_LONG, 290).

/* the private SSL-certificate as a "blob" */
curlopt(xCURLOPT_SSLCERT_BLOB, xCURLOPTTYPE_BLOB, 291).
curlopt(xCURLOPT_SSLKEY_BLOB, xCURLOPTTYPE_BLOB, 292).
curlopt(xCURLOPT_PROXY_SSLCERT_BLOB, xCURLOPTTYPE_BLOB, 293).
curlopt(xCURLOPT_PROXY_SSLKEY_BLOB, xCURLOPTTYPE_BLOB, 294).
curlopt(xCURLOPT_ISSUERCERT_BLOB, xCURLOPTTYPE_BLOB, 295).

/* Issuer certificate for proxy */
curlopt(xCURLOPT_PROXY_ISSUERCERT, xCURLOPTTYPE_STRINGPOINT, 296).
curlopt(xCURLOPT_PROXY_ISSUERCERT_BLOB, xCURLOPTTYPE_BLOB, 297).

/* the EC curves requested by the TLS client (RFC 8422, 5.1);
* OpenSSL support via 'set_groups'/'set_curves':
* https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set1_groups.html
*/
curlopt(xCURLOPT_SSL_EC_CURVES, xCURLOPTTYPE_STRINGPOINT, 298).

/* HSTS bitmask */
curlopt(xCURLOPT_HSTS_CTRL, xCURLOPTTYPE_LONG, 299).
/* HSTS file name */
curlopt(xCURLOPT_HSTS, xCURLOPTTYPE_STRINGPOINT, 300).

/* HSTS read callback */
curlopt(xCURLOPT_HSTSREADFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 301).
curlopt(xCURLOPT_HSTSREADDATA, xCURLOPTTYPE_CBPOINT, 302).

/* HSTS write callback */
curlopt(xCURLOPT_HSTSWRITEFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 303).
curlopt(xCURLOPT_HSTSWRITEDATA, xCURLOPTTYPE_CBPOINT, 304).

/* Parameters for V4 signature */
curlopt(xCURLOPT_AWS_SIGV4, xCURLOPTTYPE_STRINGPOINT, 305).

/* Same as xCURLOPT_SSL_VERIFYPEER but for DoH (DNS-over-HTTPS) servers. */
curlopt(xCURLOPT_DOH_SSL_VERIFYPEER, xCURLOPTTYPE_LONG, 306).

/* Same as xCURLOPT_SSL_VERIFYHOST but for DoH (DNS-over-HTTPS) servers. */
curlopt(xCURLOPT_DOH_SSL_VERIFYHOST, xCURLOPTTYPE_LONG, 307).

/* Same as xCURLOPT_SSL_VERIFYSTATUS but for DoH (DNS-over-HTTPS) servers. */
curlopt(xCURLOPT_DOH_SSL_VERIFYSTATUS, xCURLOPTTYPE_LONG, 308).

/* The CA certificates as "blob" used to validate the peer certificate
 this option is used only if SSL_VERIFYPEER is true */
curlopt(xCURLOPT_CAINFO_BLOB, xCURLOPTTYPE_BLOB, 309).

/* The CA certificates as "blob" used to validate the proxy certificate
 this option is used only if PROXY_SSL_VERIFYPEER is true */
curlopt(xCURLOPT_PROXY_CAINFO_BLOB, xCURLOPTTYPE_BLOB, 310).

/* used by scp/sftp to verify the host's public key */
curlopt(xCURLOPT_SSH_HOST_PUBLIC_KEY_SHA256, xCURLOPTTYPE_STRINGPOINT, 311).

/* Function that will be called immediately before the initial request
 is made on a connection (after any protocol negotiation step).  */
curlopt(xCURLOPT_PREREQFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 312).

/* Data passed to the xCURLOPT_PREREQFUNCTION callback */
curlopt(xCURLOPT_PREREQDATA, xCURLOPTTYPE_CBPOINT, 313).

/* maximum age (since creation) of a connection to consider it for reuse
* (in seconds) */
curlopt(xCURLOPT_MAXLIFETIME_CONN, xCURLOPTTYPE_LONG, 314).

/* Set MIME option flags. */
curlopt(xCURLOPT_MIME_OPTIONS, xCURLOPTTYPE_LONG, 315).

/* set the SSH host key callback, must point to a curl_sshkeycallback
 function */
curlopt(xCURLOPT_SSH_HOSTKEYFUNCTION, xCURLOPTTYPE_FUNCTIONPOINT, 316).

/* set the SSH host key callback custom pointer */
curlopt(xCURLOPT_SSH_HOSTKEYDATA, xCURLOPTTYPE_CBPOINT, 317).

/* specify which protocols that are allowed to be used for the transfer,
 which thus helps the app which takes URLs from users or other external
 inputs and want to restrict what protocol(s) to deal with. Defaults to
 all built-in protocols. */
curlopt(xCURLOPT_PROTOCOLS_STR, xCURLOPTTYPE_STRINGPOINT, 318).

/* specify which protocols that libcurl is allowed to follow directs to */
curlopt(xCURLOPT_REDIR_PROTOCOLS_STR, xCURLOPTTYPE_STRINGPOINT, 319).

