#ifndef MNFCGI_H_DEFINED
#define MNFCGI_H_DEFINED

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h> /* bool/false/true */
#include <stddef.h> /* offsetof et al. */
#include <stdint.h> /* uintX_t etc */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <mrkcommon/bytes.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/bytestream.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public Interfaces
 */

#ifndef MNFCGI_RECORD_T_DEFINED
union _mnfcgi_record;
typedef union _mnfcgi_record mnfcgi_record_t;
#define MNFCGI_RECORD_T_DEFINED
#endif

#ifndef MNFCGI_CONFIG_T_DEFINED
struct _mnfcgi_config;
typedef struct _mnfcgi_config mnfcgi_config_t;
#define MNFCGI_CONFIG_T_DEFINED
#endif

#ifndef MNFCGI_REQUEST_T_DEFINED
struct _mnfcgi_request {
    /*
     * public
     */
    /*
     * app specific data, for example method selector in an API application.
     */
    void *udata;

    struct {
#define MNFCGI_REQUEST_SCHEME_HTTP      0
#define MNFCGI_REQUEST_SCHEME_HTTPS     1
        int scheme;

#define MNFCGI_REQUEST_METHOD_GET       0
#define MNFCGI_REQUEST_METHOD_HEAD      1
#define MNFCGI_REQUEST_METHOD_POST      2
#define MNFCGI_REQUEST_METHOD_PUT       3
#define MNFCGI_REQUEST_METHOD_DELETE    4
#define MNFCGI_REQUEST_METHOD_OPTIONS   5
#define MNFCGI_REQUEST_METHOD_UNKNOWN   6
#define MNFCGI_REQUEST_METHOD_COUNT     7
        int method;

        mnbytes_t *script_name;
        mnhash_t query_terms;
        mnbytes_t *content_type;
        size_t content_length;
    } info;
};
typedef struct _mnfcgi_request mnfcgi_request_t;

#define MNFCGI_REQUEST_SCHEME_HTTP      0
#define MNFCGI_REQUEST_SCHEME_HTTPS     1

#define MNFCGI_REQUEST_METHOD_GET       0
#define MNFCGI_REQUEST_METHOD_HEAD      1
#define MNFCGI_REQUEST_METHOD_POST      2
#define MNFCGI_REQUEST_METHOD_PUT       3
#define MNFCGI_REQUEST_METHOD_DELETE    4
#define MNFCGI_REQUEST_METHOD_OPTIONS   5
#define MNFCGI_REQUEST_METHOD_UNKNOWN   6
#define MNFCGI_REQUEST_METHOD_COUNT     7

#define MNFCGI_REQUEST_T_DEFINED
#endif

#ifndef MNFCGI_RENDERER_T_DEFINED
typedef ssize_t (*mnfcgi_renderer_t)(union _mnfcgi_record *,
                                     mnbytestream_t *,
                                     void *);
#define MNFCGI_RENDERER_T_DEFINED
#endif

/*
 * mnfcgi_config_t
 */
void mnfcgi_config_init(mnfcgi_config_t *, const char *, const char *, int, int);
void mnfcgi_config_fini(mnfcgi_config_t *);
int mnfcgi_serve(mnfcgi_config_t *);


/*
 * request
 */
#define MNFCGI_REQUEST_COMPLETED (-1)
#define MNFCGI_IO_ERROR (-2)
#define MNFCGI_PAYLOAD_TOO_LARGE (-3)
#define MNFCGI_USER_ERROR_BEGIN_REQUEST (-4)
#define MNFCGI_USER_ERROR_PARAMS (-5)
#define MNFCGI_USER_ERROR_STDIN (-6)
#define MNFCGI_REQUEST_STATE    (-7)

#define MNFCHI_ESTR(e)                                                         \
((e) == MNFCGI_REQUEST_COMPLETED ? "MNFCGI_REQUEST_COMPLETED" :                \
 (e) == MNFCGI_IO_ERROR ? "MNFCGI_IO_ERROR" :                                  \
 (e) == MNFCGI_PAYLOAD_TOO_LARGE ? "MNFCGI_PAYLOAD_TOO_LARGE" :                \
 (e) == MNFCGI_USER_ERROR_BEGIN_REQUEST ? "MNFCGI_USER_ERROR_BEGIN_REQUEST" :  \
 (e) == MNFCGI_USER_ERROR_PARAMS ? "MNFCGI_USER_ERROR_PARAMS" :                \
 (e) == MNFCGI_USER_ERROR_STDIN ? "MNFCGI_USER_ERROR_STDIN" :                  \
 (e) == MNFCGI_REQUEST_STATE ? "MNFCGI_REQUEST_STATE" :                        \
 "<unknown>"                                                                   \
)                                                                              \


#define MNFCGI_MAX_PAYLOAD (0x8000)
int mnfcgi_render_stdout(mnfcgi_request_t *,
                         mnfcgi_renderer_t,
                         void *);
int mnfcgi_flush_out(mnfcgi_request_t *);
int mnfcgi_finalize_request(mnfcgi_request_t *);
mnbytes_t *mnfcgi_request_get_param(mnfcgi_request_t *,
                                    mnbytes_t *);
void mnfcgi_request_fill_info(mnfcgi_request_t *);

#define MNFCGI_FADD_IFNOEXISTS  (0x01)
#define MNFCGI_FADD_OVERRIDE    (0x02)
#define MNFCGI_FADD_DUP         (-1)
int PRINTFLIKE(4, 5) mnfcgi_request_field_addf(
        mnfcgi_request_t *,
        int,
        mnbytes_t *,
        const char *,
        ...);

int mnfcgi_request_field_addb(
        mnfcgi_request_t *,
        int,
        mnbytes_t *,
        mnbytes_t *);

int mnfcgi_request_field_addt(
        mnfcgi_request_t *,
        int,
        mnbytes_t *,
        time_t);

int mnfcgi_request_status_set(mnfcgi_request_t *, int, mnbytes_t *);
int mnfcgi_request_headers_end(mnfcgi_request_t *);

ssize_t mnfcgi_payload_size(ssize_t);
ssize_t PRINTFLIKE(2, 3) mnfcgi_printf(mnbytestream_t *, const char *, ...);
ssize_t mnfcgi_cat(mnbytestream_t *, size_t, const char *);

/*
 * ctx
 */
void mnfcgi_ctx_send_interrupt(mnfcgi_request_t *);


/*
 * util
 */
int mnfcgi_parse_qterms(mnbytes_t *, char, char, mnhash_t *);

#ifdef __cplusplus
}
#endif
#endif /* MNFCGI_H_DEFINED */
