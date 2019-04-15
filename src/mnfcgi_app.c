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
        app->callback_table.begin_request(req, app->config.udata) != 0) {
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
            app->callback_table.params_complete(req, app->config.udata) != 0) {
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
                app->callback_table.stdin_end(req, app->config.udata) != 0) {
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
            app->callback_table.end_request(req, app->config.udata) != 0) {
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

    } else {
        CTRACE("res=%s", MNFCHI_ESTR(res));
    }
    goto end;
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
mnfcgi_app_params_complete_select_exact(mnfcgi_request_t *req,
                                        UNUSED void *udata)
{
    mnhash_item_t *hit;
    mnbytes_t *key;
    mnfcgi_app_t *app;

    mnfcgi_request_fill_info(req);

    app = (mnfcgi_app_t *)req->ctx->config;
    assert(app != NULL);

    key = bytes_printf("%s%s",
            BDATASAFE(req->info.script_name),
            BDATASAFE(req->info.path_info));

    if ((hit = hash_get_item(&app->endpoint_tables, key)) == NULL) {
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


int
mnfcgi_app_params_complete_select_exact_script_name(
        mnfcgi_request_t *req, UNUSED void *udata)
{
    mnhash_item_t *hit;
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


int
mnfcgi_app_params_complete_select_exact_path_info(
        mnfcgi_request_t *req, UNUSED void *udata)
{
    mnhash_item_t *hit;
    mnfcgi_app_t *app;

    mnfcgi_request_fill_info(req);

    app = (mnfcgi_app_t *)req->ctx->config;
    assert(app != NULL);

    if (req->info.path_info == NULL ||
        (hit = hash_get_item(&app->endpoint_tables,
                             req->info.path_info)) == NULL) {
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


#if 0
int
myoauth_app_params_odata(mnfcgi_request_t *req, UNUSED void *udata)
{
    mnfcgi_app_t *app;

    mnfcgi_request_fill_info(req);
    app = (mnfcgi_app_t *)req->ctx->config;
    assert(app != NULL);

    if (req->info.script_name == NULL) {
        /* 404 */
        mnfcgi_app_error(req, 404, &_not_found);

    } else {
        mnbytes_t *script_base;
        mnhash_item_t *hit;
        mnfcgi_app_endpoint_table_t *t;


        script_base = NULL;
        if ((hit = hash_get_item(&app->endpoint_tables, script_base)) == NULL) {
            /* 404 */
            mnfcgi_app_error(req, 404, &_not_found);
        } else {
            t = hit->value;
            assert(t != NULL);
            req->udata = t->method_callback[req->info.method];
        }
    }
    return 0;
}
#endif


/*
 * return either NULL, or an (mnbytes_t *) instance with nref = 0;
 */

#ifndef UNITTEST
static
#endif
mnbytes_t *
_mnfcgi_app_get_allowed_methods(mnfcgi_app_t *app, mnbytes_t *script_name)
{
    mnbytes_t *res;
    mnhash_item_t *hit;
    res = NULL;
    if ((hit = hash_get_item(&app->endpoint_tables, script_name)) != NULL) {
        mnfcgi_app_endpoint_table_t *t;
        unsigned i;
        size_t sz;
        BYTES_ALLOCA(comma, ",");

        t = hit->value;
        assert(t != NULL);

        /*
         * XXX we rely on the fact that none of mnfcgi_request_methods[]
         * XXX items is longer than 16 chars.
         */
        res = bytes_new(countof(t->method_callback) * 16);
        memset(BDATA(res), '\0', countof(t->method_callback) * 16);
        sz = 0;
        for (i = 0; i < countof(t->method_callback); ++i) {
            if (t->method_callback[i] != NULL) {
                mnbytes_t *m;

                m = mnfcgi_request_method_str(i);
                bytes_copy(res, m, sz);
                sz += BSZ(m) - 1;
                bytes_copy(res, comma, sz);
                sz += BSZ(comma) - 1;
            }
        }
        if (sz > 0) {
            sz -= BSZ(comma) - 1;
        }
        BDATA(res)[sz] = '\0';
        ++sz;
        BSZ(res) = sz;
    }

    return res;
}


mnbytes_t *
mnfcgi_app_get_allowed_methods(mnfcgi_request_t *req)
{
    mnfcgi_app_t *app;

    app = (mnfcgi_app_t *)req->ctx->config;
    assert(app != NULL);
    assert(req->info.script_name != NULL);
    return _mnfcgi_app_get_allowed_methods(app, req->info.script_name);
}


static int
mnfcgi_app_endpoint_table_item_fini(mnbytes_t *key, void *value)
{
    BYTES_DECREF(&key);
    if (MRKLIKELY(value != NULL)) {
        free(value);
        value = NULL;
    }
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
              (hash_item_finalizer_t)mnfcgi_app_endpoint_table_item_fini);

    if (app->callback_table.init_app != NULL) {
        res = app->callback_table.init_app(app);
    }
    return res;
}


void
mnfcgi_app_incref(mnfcgi_app_t *app)
{
    ++app->config.nref;
}


int
mnfcgi_app_register_endpoint(mnfcgi_app_t *app,
                             mnfcgi_app_endpoint_table_t *table)
{
    int res;

    res = 0;
    if (MRKLIKELY(
            hash_get_item(&app->endpoint_tables, table->endpoint) != NULL)) {
        res = -1;
    } else {
        mnfcgi_app_endpoint_table_t *t;

        if (MRKUNLIKELY(
                (t = malloc(sizeof(mnfcgi_app_endpoint_table_t))) == NULL)) {
            FAIL("malloc");
        }
        *t = *table;
        hash_set_item(&app->endpoint_tables, t->endpoint, t);
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

    MNFCGI_CONFIG_INCREF(&res->config);
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
        if (--(*app)->config.nref <= 0) {
            mnfcgi_app_fini(*app);
            free(*app);
        }
        *app = NULL;
    }
}

mnfcgi_stats_t *
mnfcgi_app_get_stats(mnfcgi_app_t *app)
{
    return &app->config.stats;
}


void
mnfcgi_app_set_udata(mnfcgi_app_t *app, void *udata)
{
    app->config.udata = udata;
}
