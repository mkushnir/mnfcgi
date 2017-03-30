#include <mrkcommon/dumpm.h>
#include <mrkcommon/util.h>

#include <mrkthr.h>
#include <mrkpq.h>

// TODO: convert to use public interface <mnfcgi.h>
#include <mnfcgi_app_private.h>

#include "testoauth.h"
#include "diag.h"
#include "config.h"


/*
 * constants
 */
static mnbytes_t _not_implemented = BYTES_INITIALIZER("Not Implemented");

static mnbytes_t _server = BYTES_INITIALIZER("Server");
static mnbytes_t _date = BYTES_INITIALIZER("Date");
UNUSED static mnbytes_t _param_http_host = BYTES_INITIALIZER("HTTP_HOST");
UNUSED static mnbytes_t _x_www_form_urlencoded =
    BYTES_INITIALIZER("application/x-www-form-urlencoded");

UNUSED static mnbytes_t _content_type = BYTES_INITIALIZER("Content-Type");
static mnbytes_t _cache_control = BYTES_INITIALIZER("Cache-Control");
static mnbytes_t _private = BYTES_INITIALIZER("private");
static mnbytes_t _pragma = BYTES_INITIALIZER("Pragma");
static mnbytes_t _no_cache = BYTES_INITIALIZER("no-cache");


/*
 * endpoints
 */
static mnbytes_t __login = BYTES_INITIALIZER("/login");
static mnfcgi_app_endpoint_table_t login_table = {
    &__login, {testoauth_login_get, NULL,}
};


static mnbytes_t __token = BYTES_INITIALIZER("/token");
static mnfcgi_app_endpoint_table_t token_table = {
    &__token, { NULL,}
};


static mnbytes_t __client = BYTES_INITIALIZER("/client");
static mnfcgi_app_endpoint_table_t client_table = {
    &__client, { NULL,}
};


/*
 * database (postgres)
 */
mrkpq_cache_t cache;


int
testoauth_begin_request(UNUSED mnfcgi_request_t *req,
                        UNUSED void *udata)
{
    //CTRACE("initializing %p", req);
    return 0;
}


//static int
//myshutdown(UNUSED int argc, UNUSED void **argv)
//{
//
//    mrkthr_sleep(1000);
//    mrkthr_shutdown();
//    return 0;
//}


int
testoauth_data(UNUSED mnfcgi_request_t *req,
               UNUSED void *udata)
{
    //CTRACE("data ...");
    return 0;
}


int
testoauth_stdin(UNUSED mnfcgi_request_t *req,
                UNUSED void *udata)
{
    //CTRACE("stdin ...");
    return 0;
}


int
testoauth_stdin_end(mnfcgi_request_t *req, void *udata)
{
    mnfcgi_app_callback_t cb;

    (void)mnfcgi_request_field_addf(req, 0,
            &_server, "%s/%s", PACKAGE, VERSION);
    (void)mnfcgi_request_field_addt(req, 0,
            &_date, MRKTHR_GET_NOW_SEC() - 3600);
    (void)mnfcgi_request_field_addb(req, 0,
            &_cache_control, &_private);
    (void)mnfcgi_request_field_addb(req, 0,
            &_pragma, &_no_cache);

    if ((cb = req->udata) != NULL) {
        (void)cb(req, udata);

    } else {
        mnfcgi_app_error(req, 501, &_not_implemented);
    }

    if (MRKUNLIKELY(mnfcgi_finalize_request(req) != 0)) {
    }
    /*
     * at this point no operation on req is possible any more.
     */
    return 0;
}


int
testoauth_end_request(UNUSED mnfcgi_request_t *req,
                      UNUSED void *udata)
{
    //CTRACE("finalizing %p", req);
    return 0;
}


int
testoauth_app_init(mnfcgi_app_t *app)
{
    unsigned i;
    mnfcgi_app_endpoint_table_t *endpoints[] = {
        &login_table,
        &token_table,
        &client_table,
    };

    for (i = 0; i < countof(endpoints); ++i) {
        if (MRKUNLIKELY(mnfcgi_app_register_endpoint(app,
                                                     endpoints[i])) != 0) {
            FAIL("testoauth_app_init");
        }
    }

    return mrkpq_cache_init(&cache, "postgres://postgres@localhost/auth");
}


int
testoauth_app_fini(UNUSED mnfcgi_app_t *app)
{
    mrkpq_cache_fini(&cache);
    return 0;
}
