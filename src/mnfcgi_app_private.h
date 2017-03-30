#ifndef MNFCGI_APP_PRIVATE_H_DEFINED
#define MNFCGI_APP_PRIVATE_H_DEFINED
#include "mnfcgi_private.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef int (*mnfcgi_app_callback_t)(mnfcgi_request_t *, void *);
#define MNFCGI_APP_CALLBACK_T_DEFINED

struct _mnfcgi_app;

typedef int (*mnfcgi_app_initializer_t)(struct _mnfcgi_app *);
#define MNFCGI_APP_INITIALIZER_T_DEFINED
typedef int (*mnfcgi_app_finalizer_t)(struct _mnfcgi_app *);
#define MNFCGI_APP_FINALIZER_T_DEFINED

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


typedef struct _mnfcgi_app {
    mnfcgi_config_t config;
    mnfcgi_app_callback_table_t callback_table;
    mnhash_t endpoint_tables;
} mnfcgi_app_t;
#define MNFCGI_APP_T_DEFINED


#ifdef __cplusplus
}
#endif

#include <mnfcgi_app.h>

#endif
