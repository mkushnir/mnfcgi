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

#include <mndiag.h>

#include <mrkcommon/bytes.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/bytestream.h>

#include <mnhttp.h>

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

#ifndef MNFCGI_STATS_T_DEFINED
struct _mnfcgi_stats {
    int nthreads;
};
typedef struct _mnfcgi_stats mnfcgi_stats_t;
#define MNFCGI_STATS_T_DEFINED
#endif


#ifndef MNFCGI_REQUEST_SCHEME_T_DEFINED
typedef enum _mnfcgi_request_scheme {
    MNFCGI_REQUEST_SCHEME_HTTP =    0,
    MNFCGI_REQUEST_SCHEME_HTTPS =   1,
} mnfcgi_request_scheme_t;
#define MNFCGI_REQUEST_SCHEME_T_DEFINED
#endif

#ifndef MNFCGI_REQUEST_METHOD_T_DEFINED
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
        mnfcgi_request_scheme_t scheme;
        mnfcgi_request_method_t method;
        mnbytes_t *script_name;
        mnbytes_t *path_info;
        mnhash_t query_terms;
        mnhash_t cookie;
        mnbytes_t *content_type;
        size_t content_length;
    } info;
};
typedef struct _mnfcgi_request mnfcgi_request_t;
#define MNFCGI_REQUEST_T_DEFINED
#endif

#ifndef MNFCGI_RENDERER_T_DEFINED
typedef ssize_t (*mnfcgi_renderer_t)(union _mnfcgi_record *,
                                     mnbytestream_t *,
                                     void *);
#define MNFCGI_RENDERER_T_DEFINED
#endif

void *mnfcgi_stdout_get_udata(mnfcgi_record_t *);
void *mnfcgi_stderr_get_udata(mnfcgi_record_t *);

/*
 * mnfcgi_config_t
 */
int mnfcgi_serve(mnfcgi_config_t *);
mnfcgi_stats_t *mnfcgi_config_get_stats(mnfcgi_config_t *);

/*
 * diag.txt
 */
#ifndef MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR
#define MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR (128)
#endif

/*
 * request
 */
#define MNFCGI_REQUEST_COMPLETED                               \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       1)                                      \


#define MNFCGI_IO_ERROR                                        \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       2)                                      \


#define MNFCGI_PAYLOAD_TOO_LARGE                               \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       3)                                      \


#define MNFCGI_USER_ERROR_BEGIN_REQUEST                        \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       4)                                      \


#define MNFCGI_USER_ERROR_PARAMS                               \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       5)                                      \


#define MNFCGI_USER_ERROR_STDIN                                \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       6)                                      \


#define MNFCGI_REQUEST_STATE                                   \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       7)                                      \


#define MNFCHI_ESTR(e)                         \
((e) == (int)MNFCGI_REQUEST_COMPLETED ?        \
    "MNFCGI_REQUEST_COMPLETED" :               \
 (e) == (int)MNFCGI_IO_ERROR ?                 \
    "MNFCGI_IO_ERROR" :                        \
 (e) == (int)MNFCGI_PAYLOAD_TOO_LARGE ?        \
    "MNFCGI_PAYLOAD_TOO_LARGE" :               \
 (e) == (int)MNFCGI_USER_ERROR_BEGIN_REQUEST ? \
    "MNFCGI_USER_ERROR_BEGIN_REQUEST" :        \
 (e) == (int)MNFCGI_USER_ERROR_PARAMS ?        \
    "MNFCGI_USER_ERROR_PARAMS" :               \
 (e) == (int)MNFCGI_USER_ERROR_STDIN ?         \
    "MNFCGI_USER_ERROR_STDIN" :                \
 (e) == (int)MNFCGI_REQUEST_STATE ?            \
    "MNFCGI_REQUEST_STATE" :                   \
 "<unknown>"                                   \
)                                              \


#define MNFCGI_MAX_PAYLOAD (0x8000)
int mnfcgi_render_stdout(mnfcgi_request_t *,
                         mnfcgi_renderer_t,
                         void *);
int mnfcgi_flush_out(mnfcgi_request_t *);
int mnfcgi_finalize_request(mnfcgi_request_t *);
void mnfcgi_request_fill_info(mnfcgi_request_t *);
mnbytes_t *mnfcgi_request_get_param(mnfcgi_request_t *, mnbytes_t *);
mnbytes_t *mnfcgi_request_get_query_term(mnfcgi_request_t *, mnbytes_t *);


#define MNFCGI_GET_QTN_ENULL                                   \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       8)                                      \


#define MNFCGI_GET_QTN_EINVAL                                  \
    MNDIAG_PUBLIC_CODE(MNDIAG_LIBRARY_MNFCGI,                  \
                       MNDIAG_CLASS_MNFCGI_MNFCGI_ERROR,       \
                       9)                                      \


int mnfcgi_request_get_query_term_num(mnfcgi_request_t *,
                                      mnbytes_t *,
                                      int,
                                      intmax_t *);
mnbytes_t *mnfcgi_request_get_cookie(mnfcgi_request_t *, mnbytes_t *);

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
mnbytes_t *mnfcgi_request_method_str(unsigned);
void mndiag_mnfcgi_str(int, char *, size_t);

#ifdef __cplusplus
}
#endif
#endif /* MNFCGI_H_DEFINED */
