#include "mnfcgi_app_private.h"

#include "diag.h"


static mnbytes_t _content_length = BYTES_INITIALIZER("Content-Length");
static mnbytes_t _location = BYTES_INITIALIZER("Location");
static mnbytes_t _not_found = BYTES_INITIALIZER("Not Found");


static ssize_t
mnfcgi_app_begin_request(mnfcgi_record_t *rec,
                         UNUSED mnbytestream_t *bs,
                         void *udata)
{
    mnfcgi_request_t *req = udata;
    mnfcgi_app_t *app = (mnfcgi_app_t *)req->ctx->config;

    assert(app != NULL);
    req->state = MNFCGI_REQUEST_STATE_BEGIN;
    if (app->callback_table.begin_request != NULL &&
        app->callback_table.begin_request(req, NULL) != 0) {
        return MNFCGI_USER_ERROR_BEGIN_REQUEST;
    }
    return rec->header.rsz;
}


static ssize_t
mnfcgi_app_params(mnfcgi_record_t *rec, UNUSED mnbytestream_t *bs, void *udata)
{
    mnfcgi_request_t *req = udata;
    mnfcgi_app_t *app = (mnfcgi_app_t *)req->ctx->config;

    assert(app != NULL);
    if (rec->header.rsz == 0) {
        req->state = MNFCGI_REQUEST_STATE_PARAMS_DONE;
        if (app->callback_table.params_complete != NULL &&
            app->callback_table.params_complete(req, NULL) != 0) {
            return MNFCGI_USER_ERROR_PARAMS;
        }
        req->state = MNFCGI_REQUEST_STATE_HEADERS_ALLOWED;
    }
    return rec->header.rsz;
}


static ssize_t
mnfcgi_app_stdin(mnfcgi_record_t *rec, UNUSED mnbytestream_t *bs, void *udata)
{
    mnfcgi_request_t *req = udata;
    mnfcgi_app_t *app = (mnfcgi_app_t *)req->ctx->config;

    assert(app != NULL);
    if (rec->header.rsz > 0) {
        if (app->callback_table._stdin != NULL &&
                app->callback_table._stdin(req, NULL) != 0) {
            return MNFCGI_USER_ERROR_STDIN;
        }
    } else {
        /* end of stdin */
        if (app->callback_table.stdin_end != NULL &&
                app->callback_table.stdin_end(req, NULL) != 0) {
            return MNFCGI_USER_ERROR_STDIN;
        }
    }
    return rec->header.rsz;
}


static ssize_t
mnfcgi_app_end_request(UNUSED mnfcgi_record_t *rec,
                       UNUSED mnbytestream_t *bs,
                       void *udata)
{
    mnfcgi_request_t *req = udata;

    assert(rec == NULL);
    mnfcgi_app_t *app = (mnfcgi_app_t *)req->ctx->config;

    assert(app != NULL);
    if (app->callback_table.end_request != NULL &&
            app->callback_table.end_request(req, NULL) != 0) {
        /* silence it */
    }
    return 0;
}


void
mnfcgi_app_error(mnfcgi_request_t *req, int code, mnbytes_t *text)
{
    int res;

    if (MRKUNLIKELY(
        (res = mnfcgi_request_field_addf(
            req, 0, &_content_length, "0")) != 0)) {
        goto err;
    }

    if (MRKUNLIKELY((res = mnfcgi_request_status_set(req, code, text)) != 0)) {
        goto err;
    }
    if (MRKUNLIKELY((res = mnfcgi_request_headers_end(req)) != 0)) {
        goto err;
    }
    if (MRKUNLIKELY((res = mnfcgi_finalize_request(req)) != 0)) {
        goto err;
    }

end:
    return;

err:
    if (res == MNFCGI_REQUEST_STATE) {
        CTRACE("MNFCGI_REQUEST_STATE violation");
        goto end;

    } else {
        FAIL("mnfcgi_app_error");
    }
}


void
mnfcgi_app_redir(mnfcgi_request_t *req,
                 int code,
                 mnbytes_t *text,
                 mnbytes_t *uri)
{
    int res;

    if (MRKUNLIKELY(
        (res = mnfcgi_request_field_addf(
            req, 0, &_content_length, "0")) != 0)) {
        goto err;
    }

    if (MRKUNLIKELY(
        (res = mnfcgi_request_field_addb(req, 0, &_location, uri)) != 0)) {
        goto err;
    }

    if (MRKUNLIKELY((res = mnfcgi_request_status_set(req, code, text)) != 0)) {
        goto err;
    }

    if (MRKUNLIKELY((res = mnfcgi_request_headers_end(req)) != 0)) {
        goto err;
    }

    if (MRKUNLIKELY((res = mnfcgi_finalize_request(req)) != 0)) {
        goto err;
    }

end:
    return;

err:
    if (res == MNFCGI_REQUEST_STATE) {
        CTRACE("MNFCGI_REQUEST_STATE violation");

    } else {
        CTRACE("res=%s", MNFCHI_ESTR(res));
    }
    goto end;
}


int
mnfcgi_app_params_complete(mnfcgi_request_t *req,
                           UNUSED void *udata)
{


    mnhash_item_t *hit;
    UNUSED mnhash_iter_t it;
    mnfcgi_app_t *app;

    mnfcgi_request_fill_info(req);

    app = (mnfcgi_app_t *)req->ctx->config;
    assert(app != NULL);

    if (req->info.script_name == NULL ||
        (hit = hash_get_item(&app->endpoint_tables,
                             req->info.script_name)) == NULL) {
        /* 404 */
        mnfcgi_app_error(req, 404, &_not_found);

    } else {
        mnfcgi_app_endpoint_table_t *t;

        t = hit->value;
        assert(t != NULL);
        req->udata = t->method_callback[req->info.method];
    }

    //CTRACE("params ...");
    return 0;
}


static int
mnfcgi_app_init(mnfcgi_app_t *app,
                const char *host,
                const char *port,
                int max_conn,
                int max_req,
                mnfcgi_app_callback_table_t *table)
{
    int res;

    res = 0;

    mnfcgi_config_init(&app->config, host, port, max_conn, max_req);

    app->config.begin_request_parse = mnfcgi_app_begin_request;
    app->config.params_parse = mnfcgi_app_params;
    app->config.stdin_parse = mnfcgi_app_stdin;
    app->config.end_request_render = mnfcgi_app_end_request;

    if (table != NULL) {
        app->callback_table = *table;
    } else {
        memset(&app->callback_table, '\0', sizeof(mnfcgi_app_callback_table_t));
    }

    hash_init(&app->endpoint_tables, 61,
              (hash_hashfn_t)bytes_hash,
              (hash_item_comparator_t)bytes_cmp,
              NULL);

    if (app->callback_table.init_app != NULL) {
        res = app->callback_table.init_app(app);
    }
    return res;
}


int
mnfcgi_app_register_endpoint(mnfcgi_app_t *app,
                             mnfcgi_app_endpoint_table_t *table)
{
    int res;

    res = 0;
    if (MRKLIKELY(hash_get_item(&app->endpoint_tables, table->endpoint) != NULL)) {
        res = -1;
    } else {
        hash_set_item(&app->endpoint_tables, table->endpoint, table);
        BYTES_INCREF(table->endpoint);
    }
    return res;
}

static void
mnfcgi_app_fini(mnfcgi_app_t *app)
{
    if (app->callback_table.fini_app != NULL) {
        (void)app->callback_table.fini_app(app);
    }
    hash_fini(&app->endpoint_tables);
    mnfcgi_config_fini(&app->config);
}


mnfcgi_app_t *
mnfcgi_app_new(const char *host,
               const char *port,
               int max_conn,
               int max_req,
               mnfcgi_app_callback_table_t *table)
{
    mnfcgi_app_t *res;
    if (MRKUNLIKELY((res = malloc(sizeof(mnfcgi_app_t))) == NULL)) {
        FAIL("malloc");
    }
    if (mnfcgi_app_init(res, host, port, max_conn, max_req, table) != 0) {
        goto err;
    }

end:
    return res;

err:
    mnfcgi_app_destroy(&res);
    goto end;
}


void
mnfcgi_app_destroy(mnfcgi_app_t **app)
{
    if (*app != NULL) {
        mnfcgi_app_fini(*app);
        free(*app);
        *app = NULL;
    }
}
