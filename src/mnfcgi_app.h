#ifndef MNFCGI_APP_H_DEFINED
#define MNFCGI_APP_H_DEFINED
#include <mnfcgi.h>

#ifdef __cplusplus
extern "C" {
#endif
/*
 * ???
 */

#ifndef MNFCGI_APP_CALLBACK_T_DEFINED
typedef int (*mnfcgi_app_callback_t)(mnfcgi_request_t *, void *);
#define MNFCGI_APP_CALLBACK_T_DEFINED
#endif

#ifndef MNFCGI_APP_T_DEFINED
#define MNFCGI_APP_T_DEFINED
struct _mnfcgi_app;
typedef struct _mnfcgi_app mnfcgi_app_t;
#endif

#ifndef MNFCGI_APP_INITIALIZER_T_DEFINED
typedef int (*mnfcgi_app_initializer_t)(mnfcgi_app_t *);
#define MNFCGI_APP_INITIALIZER_T_DEFINED
#endif

#ifndef MNFCGI_APP_FINALIZER_T_DEFINED
typedef int (*mnfcgi_app_finalizer_t)(mnfcgi_app_t *);
#define MNFCGI_APP_FINALIZER_T_DEFINED
#endif

#ifndef MNFCGI_APP_CALLBACK_TABLE_T_DEFINED
/*
 * An extension to mnfcgi_config_t.
 */
typedef struct _mnfcgi_app_callback_table {
    mnfcgi_app_initializer_t init_app;
    mnfcgi_app_callback_t begin_request;
    mnfcgi_app_callback_t params_complete;
    mnfcgi_app_callback_t _stdin;
    mnfcgi_app_callback_t data;
    mnfcgi_app_callback_t stdin_end;
    mnfcgi_app_callback_t end_request;
    mnfcgi_app_finalizer_t fini_app;
} mnfcgi_app_callback_table_t;
#define MNFCGI_APP_CALLBACK_TABLE_T_DEFINED
#endif


/*
 * Defines an API endpoint, and can be accessed as
 * (mnfcgi_app_callback_table_t *)mnfcgi_request_t::udata
 */
typedef struct _mnfcgi_app_endpoint_table {
    mnbytes_t *endpoint;
    mnfcgi_app_callback_t method_callback[MNFCGI_REQUEST_METHOD_COUNT];
} mnfcgi_app_endpoint_table_t;


mnfcgi_app_t *mnfcgi_app_new(const char *,
                             const char *,
                             int,
                             int,
                             mnfcgi_app_callback_table_t *);

int mnfcgi_app_register_endpoint(mnfcgi_app_t *,
                                 mnfcgi_app_endpoint_table_t *);

#define mnfcgi_app_serve(app) (mnfcgi_serve((mnfcgi_config_t *)app))

void mnfcgi_app_destroy(mnfcgi_app_t **);


int mnfcgi_app_params_complete(mnfcgi_request_t *, void *);

void mnfcgi_app_error(mnfcgi_request_t *, int, mnbytes_t *);
void mnfcgi_app_redir(mnfcgi_request_t *, int, mnbytes_t *, mnbytes_t *);


#ifdef __cplusplus
}
#endif
#endif
