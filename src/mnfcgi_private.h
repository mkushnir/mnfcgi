#ifndef MNFCGI_PRIVATE_H_DEFINED
#define MNFCGI_PRIVATE_H_DEFINED

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <stdio.h>

#include <mrkcommon/bytes.h>
#include <mrkcommon/bytestream.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/stqueue.h>

#include <mrkthr.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * https://tools.ietf.org/html/rfc3875
 * https://github.com/FastCGI-Archives/fcgi2/blob/master/doc/fcgi-spec.html
 */

/*
 * Private Interfaces
 */

/*
 * wire
 */
union _mnfcgi_record;

#define MNFCGI_HEADER_LEN           8
typedef struct _mnfcgi_header {
    STQUEUE_ENTRY(_mnfcgi_header, link);
#define MNFCGI_VERSION              1
    uint8_t version;
#define MNFCGI_BEGIN_REQUEST        1
#define MNFCGI_ABORT_REQUEST        2
#define MNFCGI_END_REQUEST          3
#define MNFCGI_PARAMS               4
#define MNFCGI_STDIN                5
#define MNFCGI_STDOUT               6
#define MNFCGI_STDERR               7
#define MNFCGI_DATA                 8
#define MNFCGI_GET_VALUES           9
#define MNFCGI_GET_VALUES_RESULT    10
#define MNFCGI_UNKNOWN_TYPE         11
#define MNFCGI_MAXTYPE (MNFCGI_UNKNOWN_TYPE)

#define MNFCGI_TYPE_STR(ty)                                    \
((ty) == MNFCGI_BEGIN_REQUEST ? "FCGI_BEGIN_REQUEST" :         \
 (ty) == MNFCGI_ABORT_REQUEST ? "FCGI_ABORT_REQUEST" :         \
 (ty) == MNFCGI_END_REQUEST ? "FCGI_END_REQUEST" :             \
 (ty) == MNFCGI_PARAMS ? "FCGI_PARAMS" :                       \
 (ty) == MNFCGI_STDIN ? "FCGI_STDIN" :                         \
 (ty) == MNFCGI_STDOUT ? "FCGI_STDOUT" :                       \
 (ty) == MNFCGI_STDERR ? "FCGI_STDERR" :                       \
 (ty) == MNFCGI_DATA ? "FCGI_DATA" :                           \
 (ty) == MNFCGI_GET_VALUES ? "FCGI_GET_VALUES" :               \
 (ty) == MNFCGI_GET_VALUES_RESULT ? "FCGI_GET_VALUES_RESULT" : \
 (ty) == MNFCGI_UNKNOWN_TYPE ? "FCGI_UNKNOWN_TYPE" :           \
 "<unknown>"                                                   \
 )                                                             \

    uint8_t type;
#define MNFCGI_RID_NULL             0
    uint16_t rid;
    uint16_t rsz;

#define MNFCGI_HEADER_PADDING(rec)                     \
    ((rec->header.rsz % 8) ? 8 - rec->header.rsz % 8 : 0)  \


#define MNFCGI_PADDING_VALUE ((uint8_t)0x5a)
    uint8_t psz;
    uint8_t reserved;
} mnfcgi_header_t;


/*
 * Application
 */

/**
 * Lifetime of `union _mnfcgi_record' is limited to the execution scope of
 * the callback.
 *
 * Possible cases for the udata argument for record types, otherwise NULL:
 *
 *  - MNFCGI_PARAMS: mnfcgi_request_t *
 *  - MNFCGI_STDIN: mnfcgi_request_t *
 *  - MNFCGI_DATA: mnfcgi_request_t *
 *  - MNFCGI_STDOUT: mnfcgi_request_t *
 *
 * In all cases, the lifetime of the `mnfcgi_request_t *' instance is limited
 * to the execution scope of the callback.
 *
 */
typedef ssize_t (*mnfcgi_renderer_t)(union _mnfcgi_record *,
                                     mnbytestream_t *,
                                     void *);
#define MNFCGI_RENDERER_T_DEFINED

typedef ssize_t (*mnfcgi_parser_t)(union _mnfcgi_record *,
                                   mnbytestream_t *,
                                   void *);

/*
 * MNFCGI_BEGIN_REQUEST (in)
 */
typedef struct _mnfcgi_begin_request {
    mnfcgi_header_t header;
#define MNFCGI_RESPONDER    1
#define MNFCGI_AUTHORIZER   2
#define MNFCGI_FILTER       3
    uint16_t role;
#define MNFCGI_KEEP_CONN    1
    uint8_t flags;
} mnfcgi_begin_request_t;


/*
 * MNFCGI_ABORT_REQUEST (in)
 */
typedef struct _mnfcgi_abort_request {
    mnfcgi_header_t header;
} mnfcgi_abort_request_t;

/*
 * MNFCGI_END_REQUEST (out)
 */
typedef struct _mnfcgi_end_request {
    mnfcgi_header_t header;
#define MNFCGI_REQUEST_COMPLETE     0
#define MNFCGI_CANT_MPX_CONN        1
#define MNFCGI_OVERLOADED           2
#define MNFCGI_UNKNOWN_ROLE         3
    uint8_t proto_status;
    uint32_t app_status;
} mnfcgi_end_request_t;


/*
 * MNFCGI_PARAMS (in) stream
 */
typedef struct _mnfcgi_params {
    mnfcgi_header_t header;
    mnhash_t params;
} mnfcgi_params_t;


/*
 * MNFCGI_STDIN (in) stream
 */
typedef struct _mnfcgi_stdin {
    mnfcgi_header_t header;
    byterange_t br;
    mnfcgi_parser_t parse;
} mnfcgi_stdin_t;


/*
 * MNFCGI_DATA (in) stream
 */
typedef struct _mnfcgi_data {
    mnfcgi_header_t header;
    byterange_t br;
    mnfcgi_parser_t parse;
} mnfcgi_data_t;


/*
 * MNFCGI_STDOUT (out) stream
 */
typedef struct _mnfcgi_stdout {
    mnfcgi_header_t header;
    mnfcgi_renderer_t render;
    void *udata;
} mnfcgi_stdout_t;

/*
 * MNFCGI_STDERR (out) stream
 */
typedef struct _mnfcgi_stderr {
    mnfcgi_header_t header;
    mnfcgi_renderer_t render;
    void *udata;
} mnfcgi_stderr_t;


/*
 * Management
 */
/*
 * MNFCGI_GET_VALUES (in)
 * MNFCGI_GET_VALUES_RESULT (out)
 */
#define MNFCGI_MAX_CONNS   "FCGI_MAX_CONNS"
#define MNFCGI_MAX_REQS    "FCGI_MAX_REQS"
#define MNFCGI_MPXS_CONNS  "FCGI_MPXS_CONNS"
typedef struct _mnfcgi_get_values {
    mnfcgi_header_t header;
    mnhash_t values;
} mnfcgi_get_values_t;

/*
 * MNFCGI_UNKNOWN_TYPE (out)
 */
typedef struct _mnfcgi_unknown_type {
    mnfcgi_header_t header;
    uint8_t type;
} mnfcgi_unknown_type_t;


typedef union _mnfcgi_record {
    mnfcgi_header_t header;
    mnfcgi_begin_request_t begin_request;
    mnfcgi_abort_request_t abort_request;
    mnfcgi_end_request_t end_request;
    mnfcgi_params_t params;
    mnfcgi_stdin_t _stdin;
    mnfcgi_data_t data;
    mnfcgi_stdout_t _stdout;
    mnfcgi_stderr_t _stderr;

    mnfcgi_get_values_t get_values;
    mnfcgi_unknown_type_t unknown_type;
} mnfcgi_record_t;
#define MNFCGI_RECORD_T_DEFINED

/*
 * Roles
 */

typedef struct _mnfcgi_responder {
    mnfcgi_params_t *params; /* environment */
} mnfcgi_responder_t;


/*
 * Filter
 */
#define MNFCGI_DATA_LAST_MOD    "FCGI_DATA_LAST_MOD"
#define MNFCGI_DATA_LENGTH      "FCGI_DATA_LENGTH"


/*
 * Context
 */
typedef struct _mnfcgi_config {
    mnbytes_t *host;
    mnbytes_t *port;
    int max_conn;
    int max_req;
    int fd; /* accept socket */
    /**/
    mnfcgi_parser_t begin_request_parse;
    mnfcgi_parser_t params_parse;
    mnfcgi_parser_t stdin_parse;
    mnfcgi_parser_t data_parse;
    mnfcgi_renderer_t stdout_render;
    mnfcgi_renderer_t stderr_render;
    mnfcgi_renderer_t end_request_render;
    void *udata;
} mnfcgi_config_t;
#define MNFCGI_CONFIG_T_DEFINED


/*
 * lifetime limited to the execution scope of
 * all mnfcgi_config_t.xxx_(parse|render)
 */
typedef struct _mnfcgi_ctx {
    mnfcgi_config_t *config;
    mrkthr_ctx_t *thread;
    int fd;
    void *fp;
    mnbytestream_t in;
    mnbytestream_t out;
    /* strong uint16_t, mnfcgi_request_t */
    mnhash_t requests;
} mnfcgi_ctx_t;


typedef enum _mnfcgi_request_scheme {
    MNFCGI_REQUEST_SCHEME_HTTP =    0,
    MNFCGI_REQUEST_SCHEME_HTTPS =   1,
} mnfcgi_request_scheme_t;
#define MNFCGI_REQUEST_SCHEME_T_DEFINED


typedef enum _mnfcgi_request_method {
    MNFCGI_REQUEST_METHOD_GET =     0,
    MNFCGI_REQUEST_METHOD_HEAD =    1,
    MNFCGI_REQUEST_METHOD_POST =    2,
    MNFCGI_REQUEST_METHOD_PUT =     3,
    MNFCGI_REQUEST_METHOD_DELETE =  4,
    MNFCGI_REQUEST_METHOD_PATCH =   5,
    MNFCGI_REQUEST_METHOD_OPTIONS = 6,
    MNFCGI_REQUEST_METHOD_UNKNOWN = 7,
    MNFCGI_REQUEST_METHOD_COUNT =   8,
} mnfcgi_request_method_t;
#define MNFCGI_REQUEST_METHOD_T_DEFINED

/*
 * lifetime limited to the execution scope of
 * all mnfcgi_config_t.xxx_(parse|render)
 */
typedef struct _mnfcgi_request {
    /*
     * public
     */
    /*
     * app specific data, for example method selector in an API application.
     */
    void *udata;

    struct {
        mnfcgi_request_scheme_t scheme;
        mnfcgi_request_method_t method;
        mnbytes_t *script_name;
        mnhash_t query_terms;
        mnhash_t cookie;
        mnbytes_t *content_type;
        size_t content_length;
    } info;

    /*
     * private
     */
    mnfcgi_ctx_t *ctx;
    /* strong mnbytes_t *, mnbytes_t* */
    mnhash_t headers;

    mnfcgi_record_t *begin_request;
    STQUEUE(_mnfcgi_header, params);
    STQUEUE(_mnfcgi_header, _stdin);
    STQUEUE(_mnfcgi_header, data);
    STQUEUE(_mnfcgi_header, _stdout);
    STQUEUE(_mnfcgi_header, _stderr);
#define MNFCGI_REQUEST_STATE_BEGIN              1
#define MNFCGI_REQUEST_STATE_PARAMS_DONE        2
#define MNFCGI_REQUEST_STATE_HEADERS_ALLOWED    3
#define MNFCGI_REQUEST_STATE_HEADERS_END        4
#define MNFCGI_REQUEST_STATE_BODY_ALLOWED       5
    int state;
    struct {
        int complete:1;
    } flags;
} mnfcgi_request_t;
#define MNFCGI_REQUEST_T_DEFINED


/*
 * wire
 */
mnfcgi_record_t *mnfcgi_record_new(uint8_t);

void mnfcgi_record_destroy(mnfcgi_record_t **);

mnfcgi_record_t *mnfcgi_parse(mnbytestream_t *, void *);

#define MNFCGI_RENDER_ERROR (-1)
int mnfcgi_render(mnbytestream_t *, mnfcgi_record_t *, void *);

#ifdef __cplusplus
}
#endif

#include <mnfcgi.h>

#endif /* MNFCGI_PRIVATE_H_DEFINED */
