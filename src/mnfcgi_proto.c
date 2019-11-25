#include <errno.h>
#include <inttypes.h> /* strtoimax */
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mrkcommon/bytestream.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/util.h>
#include <mrkthr.h>

#include "mnfcgi_private.h"

#include "diag.h"

#define MNFCGI_DEFAULT_BYTESTREAM_BUFSZ 1024
#define MNFCGI_CTX_REQUESTS_HASHLEN 1021


static mnbytes_t _MNFCGI_MAX_CONNS = BYTES_INITIALIZER(MNFCGI_MAX_CONNS);
static mnbytes_t _MNFCGI_MPXS_CONNS = BYTES_INITIALIZER(MNFCGI_MPXS_CONNS);
static mnbytes_t _MNFCGI_MAX_REQS = BYTES_INITIALIZER(MNFCGI_MAX_REQS);

static mnbytes_t _status = BYTES_INITIALIZER("Status");

static mnbytes_t _param_request_scheme = BYTES_INITIALIZER("REQUEST_SCHEME");
static mnbytes_t _http = BYTES_INITIALIZER("http");
static mnbytes_t _https = BYTES_INITIALIZER("https");
static mnbytes_t _param_request_method = BYTES_INITIALIZER("REQUEST_METHOD");
static mnbytes_t _param_script_name = BYTES_INITIALIZER("SCRIPT_NAME");
static mnbytes_t _param_path_info = BYTES_INITIALIZER("PATH_INFO");
static mnbytes_t _param_query_string = BYTES_INITIALIZER("QUERY_STRING");
static mnbytes_t _param_content_length = BYTES_INITIALIZER("CONTENT_LENGTH");
static mnbytes_t _param_content_type = BYTES_INITIALIZER("CONTENT_TYPE");
static mnbytes_t _param_http_cookie = BYTES_INITIALIZER("HTTP_COOKIE");

/*
 * MNFCGI_REQUEST_METHOD_*
 */
static mnbytes_t _get = BYTES_INITIALIZER("GET");
static mnbytes_t _head = BYTES_INITIALIZER("HEAD");
static mnbytes_t _post = BYTES_INITIALIZER("POST");
static mnbytes_t _put = BYTES_INITIALIZER("PUT");
static mnbytes_t _delete = BYTES_INITIALIZER("DELETE");
static mnbytes_t _patch = BYTES_INITIALIZER("PATCH");
static mnbytes_t _options = BYTES_INITIALIZER("OPTIONS");
static mnbytes_t __unknown_ = BYTES_INITIALIZER("<unknown>");
static mnbytes_t *mnfcgi_request_methods[] = {
    &_get,
    &_head,
    &_post,
    &_put,
    &_delete,
    &_patch,
    &_options,
    &__unknown_,
    NULL,
};


mnbytes_t *
mnfcgi_request_method_str(unsigned m)
{
    assert(m < countof(mnfcgi_request_methods));
    return mnfcgi_request_methods[m];
}
/*
 * mnfcgi_request_t
 */

static uint64_t
mnfcgi_request_hash(void *x)
{
    uint16_t rid = (uint16_t)(uintptr_t)x;
    return (uint64_t)rid;
}


static int
mnfcgi_request_item_cmp(void *a, void *b)
{
    uint16_t ra = (uint16_t)(uintptr_t)a;
    uint16_t rb = (uint16_t)(uintptr_t)b;
    return MNCMP(ra, rb);
}


static int
header_item_fini(mnbytes_t *key, mnbytes_t *value)
{
    BYTES_DECREF(&key);
    BYTES_DECREF(&value);
    return 0;
}


static void
mnfcgi_request_init(mnfcgi_request_t *req)
{
    req->ctx = NULL;
    hash_init(&req->headers,
              31,
              (hash_hashfn_t)bytes_hash,
              (hash_item_comparator_t)bytes_cmp,
              (hash_item_finalizer_t)header_item_fini);

    req->info.scheme = MNFCGI_REQUEST_SCHEME_HTTP;
    req->info.method = MNFCGI_REQUEST_METHOD_GET;
    req->info.script_name = NULL;
    req->info.path_info = NULL;
    hash_init(&req->info.query_terms,
              31,
              (hash_hashfn_t)bytes_hash,
              (hash_item_comparator_t)bytes_cmp,
              (hash_item_finalizer_t)header_item_fini);
    hash_init(&req->info.cookie,
              17,
              (hash_hashfn_t)bytes_hash,
              (hash_item_comparator_t)bytes_cmp,
              (hash_item_finalizer_t)header_item_fini);
    req->info.content_type = NULL;
    req->info.content_length = 0;
    req->udata = NULL;

    req->begin_request = NULL;
    STQUEUE_INIT(&req->params);
    STQUEUE_INIT(&req->_stdin);
    STQUEUE_INIT(&req->data);
    STQUEUE_INIT(&req->_stdout);
    STQUEUE_INIT(&req->_stderr);
    req->state = 0;
    req->flags.complete = 0;
}


static mnfcgi_request_t *
mnfcgi_request_new(void)
{
    mnfcgi_request_t *req;

    if (MRKUNLIKELY((req = malloc(sizeof(mnfcgi_request_t))) == NULL)) {
        FAIL("malloc");
    }
    mnfcgi_request_init(req);
    return req;
}



static void
mnfcgi_request_fini(mnfcgi_request_t *req)
{
    mnfcgi_header_t *h;

    BYTES_DECREF(&req->info.script_name);
    BYTES_DECREF(&req->info.path_info);
    hash_fini(&req->info.query_terms);
    hash_fini(&req->info.cookie);
    BYTES_DECREF(&req->info.content_type);

    mnfcgi_record_destroy(&req->begin_request);

    while ((h = STQUEUE_HEAD(&req->params)) != NULL) {
        mnfcgi_record_t *rec;

        STQUEUE_DEQUEUE(&req->params, link);
        rec = (mnfcgi_record_t *)h;
        mnfcgi_record_destroy(&rec);
    }

    while ((h = STQUEUE_HEAD(&req->_stdin)) != NULL) {
        mnfcgi_record_t *rec;

        STQUEUE_DEQUEUE(&req->_stdin, link);
        rec = (mnfcgi_record_t *)h;
        mnfcgi_record_destroy(&rec);
    }

    while ((h = STQUEUE_HEAD(&req->data)) != NULL) {
        mnfcgi_record_t *rec;

        STQUEUE_DEQUEUE(&req->data, link);
        rec = (mnfcgi_record_t *)h;
        mnfcgi_record_destroy(&rec);
    }

    while ((h = STQUEUE_HEAD(&req->_stdout)) != NULL) {
        mnfcgi_record_t *rec;

        STQUEUE_DEQUEUE(&req->_stdout, link);
        rec = (mnfcgi_record_t *)h;
        mnfcgi_record_destroy(&rec);
    }

    while ((h = STQUEUE_HEAD(&req->_stderr)) != NULL) {
        mnfcgi_record_t *rec;

        STQUEUE_DEQUEUE(&req->_stderr, link);
        rec = (mnfcgi_record_t *)h;
        mnfcgi_record_destroy(&rec);
    }

    hash_fini(&req->headers);

    if (req->ctx->config->end_request_render != NULL) {
        ssize_t nwritten;
        if (MRKUNLIKELY((nwritten = req->ctx->config->end_request_render(
                            NULL, &req->ctx->out, req)) < 0)) {
        }
    }

    req->ctx = NULL;
}


static void
mnfcgi_request_destroy(mnfcgi_request_t **req)
{
    if (*req != NULL) {
        mnfcgi_request_fini(*req);
        free(*req);
        *req = NULL;
    }
}


static int
mnfcgi_request_item_fini(UNUSED void *key, mnfcgi_request_t *req)
{
    mnfcgi_request_destroy(&req);
    return 0;
}


mnbytes_t *
mnfcgi_request_get_param(mnfcgi_request_t *req,
                         const mnbytes_t *name)
{
    mnbytes_t *res;
    mnfcgi_header_t *h;

    res = NULL;
    for (h = STQUEUE_HEAD(&req->params);
         h != NULL;
         h = STQUEUE_NEXT(link, h)) {
        mnfcgi_record_t *rec;
        mnhash_item_t *hit;

        rec = (mnfcgi_record_t *)h;
        if ((hit = hash_get_item(&rec->params.params, name)) != NULL) {
            res = hit->value;
            break;
        }
    }

    return res;
}


mnbytes_t *
mnfcgi_request_get_query_term(mnfcgi_request_t *req,
                              mnbytes_t *name)
{
    mnbytes_t *res;
    mnhash_item_t *hit;

    res = NULL;
    if ((hit = hash_get_item(&req->info.query_terms, name)) != NULL) {
        res = hit->value;
    }

    return res;
}


int
mnfcgi_request_get_query_term_intmax(mnfcgi_request_t *req,
                                     mnbytes_t *name,
                                     int radix,
                                     intmax_t *rv)
{
    int res = 0;
    mnbytes_t *v;

    v = mnfcgi_request_get_query_term(req, name);
    if (bytes_is_null_or_empty(v)) {
        res = MNFCGI_GET_QTN_ENULL;
        goto end;
    }

    *rv = strtoimax(BCDATA(v), NULL, radix);
    if (*rv == 0 && errno == EINVAL) {
        res = MNFCGI_GET_QTN_EINVAL;
    }

end:
    return res;
}


int
mnfcgi_request_get_query_term_double(mnfcgi_request_t *req,
                                     mnbytes_t *name,
                                     UNUSED int radix,
                                     double *rv)
{
    int res = 0;
    mnbytes_t *v;

    v = mnfcgi_request_get_query_term(req, name);
    if (bytes_is_null_or_empty(v)) {
        res = MNFCGI_GET_QTN_ENULL;
        goto end;
    }

    *rv = strtod(BCDATA(v), NULL);
    if (errno == ERANGE) {
        res = MNFCGI_GET_QTN_EINVAL;
    }

end:
    return res;
}


mnbytes_t *
mnfcgi_request_get_cookie(mnfcgi_request_t *req,
                          mnbytes_t *name)
{
    mnbytes_t *res;
    mnhash_item_t *hit;

    res = NULL;
    if ((hit = hash_get_item(&req->info.cookie, name)) != NULL) {
        res = hit->value;
    }

    return res;
}


void
mnfcgi_request_fill_info(mnfcgi_request_t *req)
{
    mnbytes_t *value;

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_request_scheme)) != NULL)) {
        if (bytes_cmp(value, &_http) == 0) {
            req->info.scheme = MNFCGI_REQUEST_SCHEME_HTTP;
        } else if (bytes_cmp(value, &_https) == 0) {
            req->info.scheme = MNFCGI_REQUEST_SCHEME_HTTPS;
        }
    }

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_request_method)) != NULL)) {
        if (bytes_cmp(value, &_get) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_GET;
        } else if (bytes_cmp(value, &_post) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_POST;
        } else if (bytes_cmp(value, &_head) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_HEAD;
        } else if (bytes_cmp(value, &_put) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_PUT;
        } else if (bytes_cmp(value, &_delete) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_DELETE;
        } else if (bytes_cmp(value, &_patch) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_PATCH;
        } else if (bytes_cmp(value, &_options) == 0) {
            req->info.method = MNFCGI_REQUEST_METHOD_OPTIONS;
        } else {
            req->info.method = MNFCGI_REQUEST_METHOD_UNKNOWN;
        }
    }

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_script_name)) != NULL)) {
        req->info.script_name = value;
        BYTES_INCREF(value);
    }

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_path_info)) != NULL)) {
        req->info.path_info = value;
        BYTES_INCREF(value);
    }

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_query_string)) != NULL)) {
        (void)mrkhttp_parse_qterms(value, '=', '&', &req->info.query_terms);
    }

    if ((value = mnfcgi_request_get_param(req, &_param_http_cookie)) != NULL) {
        (void)mrkhttp_parse_kvpbd(value, '=', '&', &req->info.cookie);
    }

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_content_length)) != NULL)) {
        req->info.content_length = strtoimax(BCDATA(value), NULL, 10);
    }

    if (MRKLIKELY(
        (value = mnfcgi_request_get_param(req,
                                          &_param_content_type)) != NULL)) {
        req->info.content_type = value;
        BYTES_INCREF(value);
    }

}


int PRINTFLIKE(4, 5)
mnfcgi_request_field_addf(mnfcgi_request_t *req,
                          int flags,
                          mnbytes_t *name,
                          const char *fmt,
                          ...)
{
    int res;
    mnbytes_t *value;
    mnhash_item_t *hit;
    va_list ap;

    assert(!((flags & MNFCGI_FADD_IFNOEXISTS) &&
             (flags & MNFCGI_FADD_OVERRIDE)));
    assert(name != NULL);

    if (MRKUNLIKELY(req->state > MNFCGI_REQUEST_STATE_HEADERS_ALLOWED)) {
        return MNFCGI_REQUEST_STATE;
    }
    res = 0;

    hit = hash_get_item(&req->headers, name);
    if ((flags & MNFCGI_FADD_IFNOEXISTS) && (hit != NULL)) {
        return MNFCGI_FADD_DUP;
    }

    va_start(ap, fmt);
    value = bytes_vprintf(fmt, ap);
    va_end(ap);

    if (hit != NULL) {
        if (flags & MNFCGI_FADD_OVERRIDE) {
            mnbytes_t *v;

            /* unique */
            v = hit->value;
            BYTES_DECREF(&v);
            hit->value = value;
            BYTES_INCREF(value);

        } else {
            /* dup */
            hash_set_item(&req->headers, name, value);
            BYTES_INCREF(name);
            BYTES_INCREF(value);
            res = MNFCGI_FADD_DUP;
        }
    } else {
        hash_set_item(&req->headers, name, value);
        BYTES_INCREF(name);
        BYTES_INCREF(value);
    }

    return res;
}


int
mnfcgi_request_field_addb(mnfcgi_request_t *req,
                          int flags,
                          mnbytes_t *name,
                          mnbytes_t *value)
{
    int res;
    mnhash_item_t *hit;

    assert(!((flags & MNFCGI_FADD_IFNOEXISTS) &&
             (flags & MNFCGI_FADD_OVERRIDE)));

    if (MRKUNLIKELY(req->state > MNFCGI_REQUEST_STATE_HEADERS_ALLOWED)) {
        return MNFCGI_REQUEST_STATE;
    }
    res = 0;

    hit = hash_get_item(&req->headers, name);
    if ((flags & MNFCGI_FADD_IFNOEXISTS) && (hit != NULL)) {
        return MNFCGI_FADD_DUP;
    }

    if (hit != NULL) {

        if (flags & MNFCGI_FADD_OVERRIDE) {
            mnbytes_t *v;

            /* unique */
            v = hit->value;
            BYTES_DECREF(&v);
            hit->value = value;
            BYTES_INCREF(value);

        } else {
            /* dup */
            hash_set_item(&req->headers, name, value);
            BYTES_INCREF(name);
            BYTES_INCREF(value);
            res = MNFCGI_FADD_DUP;
        }
    } else {
        hash_set_item(&req->headers, name, value);
        BYTES_INCREF(name);
        BYTES_INCREF(value);
    }

    return res;
}


int
mnfcgi_request_field_addt(mnfcgi_request_t *req,
                          int flags,
                          mnbytes_t *name,
                          time_t t)
{
    int res;
    mnhash_item_t *hit;
    mnbytes_t *value;
    size_t n;
    char buf[64];
    struct tm *tv;

    assert(!((flags & MNFCGI_FADD_IFNOEXISTS) &&
             (flags & MNFCGI_FADD_OVERRIDE)));

    if (MRKUNLIKELY(req->state > MNFCGI_REQUEST_STATE_HEADERS_ALLOWED)) {
        return MNFCGI_REQUEST_STATE;
    }
    res = 0;

    hit = hash_get_item(&req->headers, name);
    if ((flags & MNFCGI_FADD_IFNOEXISTS) && (hit != NULL)) {
        return MNFCGI_FADD_DUP;
    }

    // Wdy, DD Mmm YYYY HH:MM:SS GMT
    tv = gmtime(&t);
    n = strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT",
                 tv);
    value = bytes_new_from_str_len(buf, n);

    if (hit != NULL) {
        if (flags & MNFCGI_FADD_OVERRIDE) {
            mnbytes_t *v;

            /* unique */
            v = hit->value;
            BYTES_DECREF(&v);
            hit->value = value;
            BYTES_INCREF(value);

        } else {
            /* dup */
            hash_set_item(&req->headers, name, value);
            BYTES_INCREF(name);
            BYTES_INCREF(value);
            res = MNFCGI_FADD_DUP;
        }
    } else {
        hash_set_item(&req->headers, name, value);
        BYTES_INCREF(name);
        BYTES_INCREF(value);
    }

    return res;
}


int
mnfcgi_request_status_set(mnfcgi_request_t *req,
                          int status,
                          mnbytes_t *text)
{
    assert(text != NULL);
    return mnfcgi_request_field_addf(req,
                                     MNFCGI_FADD_OVERRIDE,
                                     &_status,
                                     "%d %s",
                                     status,
                                     BDATA(text));
}


static ssize_t
mnfcgi_render_headers(UNUSED mnfcgi_record_t *rec,
                      mnbytestream_t *bs,
                      void *udata)
{
    mnfcgi_request_t *req;
    ssize_t nwritten;
    mnhash_iter_t it;
    mnhash_item_t *hit0, *hit1;

    req = udata;
    nwritten = 0;

#define MNFCGI_RENDER_HEADERS_FMT "%s: %s\r\n"
    for (hit0 = hash_first(&req->headers, &it);
         hit0 != NULL;
         ) {
        mnbytes_t *name, *value;
        ssize_t n;

        name = hit0->key;
        value = hit0->value;

        assert(name != NULL);

        if (value == NULL) {
            n = BSZ(name) + sizeof(MNFCGI_RENDER_HEADERS_FMT);
            if (n >= MNFCGI_MAX_PAYLOAD) {
                /* discard too long headers */
                hit1 = hash_next(&req->headers, &it);
                hash_delete_pair(&req->headers, hit0);
                hit0 = hit1;
            } else {
                if ((nwritten + n) < MNFCGI_MAX_PAYLOAD) {
                    nwritten += mnfcgi_printf(bs,
                                              MNFCGI_RENDER_HEADERS_FMT,
                                              BDATA(name),
                                              "");
                    hit1 = hash_next(&req->headers, &it);
                    hash_delete_pair(&req->headers, hit0);
                    hit0 = hit1;
                } else {
                    break;
                }
            }
        } else {
            n = BSZ(name) + BSZ(value) + sizeof(MNFCGI_RENDER_HEADERS_FMT);
            if (n >= MNFCGI_MAX_PAYLOAD) {
                /* discard too long headers */
                hit1 = hash_next(&req->headers, &it);
                hash_delete_pair(&req->headers, hit0);
                hit0 = hit1;
            } else {
                if ((nwritten + n) < MNFCGI_MAX_PAYLOAD) {
                    nwritten += mnfcgi_printf(bs,
                                              MNFCGI_RENDER_HEADERS_FMT,
                                              BDATA(name),
                                              BDATA(value));
                    hit1 = hash_next(&req->headers, &it);
                    hash_delete_pair(&req->headers, hit0);
                    hit0 = hit1;
                } else {
                    break;
                }
            }
        }
    }

    return nwritten;
}


static ssize_t
mnfcgi_render_empty_line(UNUSED mnfcgi_record_t *rec,
                         mnbytestream_t *bs,
                         UNUSED void *udata)
{
    return mnfcgi_cat(bs, 2, "\r\n");
}

int
mnfcgi_request_headers_end(mnfcgi_request_t *req)
{
    int res;

    res = 0;
    if (MRKUNLIKELY(req->state > MNFCGI_REQUEST_STATE_HEADERS_ALLOWED)) {
        return MNFCGI_REQUEST_STATE;
    }

    while (!hash_is_empty(&req->headers)) {
        if ((res = mnfcgi_render_stdout(req,
                                        mnfcgi_render_headers,
                                        NULL)) != 0) {
            break;
        }
    }

    res = mnfcgi_render_stdout(req, mnfcgi_render_empty_line, NULL);

    req->state = MNFCGI_REQUEST_STATE_HEADERS_END;
    return res;
}


/*
 * mnfcgi_config_t
 */
void
mnfcgi_config_init(mnfcgi_config_t *config,
            const char *host,
            const char *port,
            int max_conn,
            int max_req)
{
    assert(host != NULL);
    assert(port != NULL);
    assert(max_conn > 0);

    config->nref = 0;
    config->host = bytes_new_from_str(host);
    BYTES_INCREF(config->host);

    config->port = bytes_new_from_str(port);
    BYTES_INCREF(config->port);

    config->max_conn = max_conn;
    config->max_req = max_req;
    config->fd = -1;

    config->params_parse = NULL;
    config->stdin_parse = NULL;
    config->data_parse = NULL;
    config->stdout_render = NULL;
    config->stderr_render = NULL;
    config->udata = NULL;
    config->stats.nthreads = 0;
}


void
mnfcgi_config_fini(mnfcgi_config_t *config)
{
    if (config->fd != -1) {
        close(config->fd);
        config->fd = -1;
    }
    BYTES_DECREF(&config->host);
    BYTES_DECREF(&config->port);
}


mnfcgi_stats_t *
mnfcgi_config_get_stats(mnfcgi_config_t *config)
{
    return &config->stats;
}


/*
 * mnfcgi_ctx_t
 */
static void
mnfcgi_ctx_init(mnfcgi_ctx_t *ctx,
                mnfcgi_config_t *config,
                int fd)
{
    ctx->config = config;
    MNFCGI_CONFIG_INCREF(ctx->config);
    ctx->thread = NULL;
    ctx->fd = fd;
    ctx->fp = (void *)(intptr_t)fd;

    bytestream_init(&ctx->in,
                    MNFCGI_DEFAULT_BYTESTREAM_BUFSZ);
    ctx->in.read_more = mrkthr_bytestream_read_more;

    bytestream_init(&ctx->out,
                    MNFCGI_DEFAULT_BYTESTREAM_BUFSZ);
    ctx->out.write = mrkthr_bytestream_write;

    hash_init(&ctx->requests,
              MNFCGI_CTX_REQUESTS_HASHLEN,
              (hash_hashfn_t)mnfcgi_request_hash,
              (hash_item_comparator_t)mnfcgi_request_item_cmp,
              (hash_item_finalizer_t)mnfcgi_request_item_fini);
}

static void
mnfcgi_ctx_fini(mnfcgi_ctx_t *ctx)
{
    if (ctx->fd != -1) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    ctx->fp = (void *)-1;
    hash_fini(&ctx->requests);
    bytestream_fini(&ctx->in);
    bytestream_fini(&ctx->out);
    MNFCGI_CONFIG_DECREF(&ctx->config);
}


void
mnfcgi_ctx_send_interrupt(mnfcgi_request_t *req)
{
    if (req->ctx->thread != NULL) {
        mrkthr_set_interrupt(req->ctx->thread);
    }
}

/*
 * Fast CGI protocol handler
 */
static int
mnfcgi_render_end_request(mnfcgi_ctx_t *ctx,
                          mnfcgi_record_t *rec,
                          uint8_t proto_status,
                          uint32_t app_status)
{
    int res;
    mnfcgi_record_t *response;

    res = 0;

    if (MRKUNLIKELY(
            (response =
             mnfcgi_record_new(MNFCGI_END_REQUEST)) == NULL)) {
        res = MNFCGI_RENDER_END_REQUEST + 1;
        goto end;
    }
    //CTRACE("rid=%hd", rec->header.rid);
    response->header.rid = rec->header.rid;
    response->end_request.proto_status = proto_status;
    response->end_request.app_status = app_status;

    if (MRKUNLIKELY((res = mnfcgi_render(&ctx->out, response, NULL)) != 0)) {
        goto end;
    }

end:
    mnfcgi_record_destroy(&response);
    return res;
}


static int
mnfcgi_render_empty_stdout(mnfcgi_ctx_t *ctx,
                         mnfcgi_request_t *req)
{
    int res;
    mnfcgi_record_t *response;

    if (req->flags.complete) {
        return MNFCGI_REQUEST_COMPLETED;
    }

    res = 0;
    if (MRKUNLIKELY(
            (response =
             mnfcgi_record_new(MNFCGI_STDOUT)) == NULL)) {
        res = MNFCGI_RENDER_EMPTY_STDOUT + 1;
        goto end;
    }
    assert(req->begin_request != NULL);
    //CTRACE("rid=%hd", req->begin_request->header.rid);
    response->header.rid = req->begin_request->header.rid;

    if (MRKUNLIKELY((res = mnfcgi_render(&ctx->out, response, NULL)) != 0)) {
        goto end;
    }

end:
    mnfcgi_record_destroy(&response);
    return res;

}


static int
mnfcgi_abort_request(mnfcgi_request_t *req, int app_status)
{
    int res;
    //mnfcgi_header_t *h;
    //mnhash_item_t *hit;

    //CTRACE("AAA");

    //while ((h = STQUEUE_HEAD(&req->_stdout)) != NULL) {
    //    mnfcgi_record_t *rec;

    //    rec = (mnfcgi_record_t *)h;
    //    mnfcgi_record_destroy(&rec);
    //}

    if (!req->flags.complete) {
        /* end request */
        if (mnfcgi_render_end_request(
                req->ctx,
                req->begin_request,
                MNFCGI_REQUEST_COMPLETE,
                app_status) != 0) {
        }

        if (MRKUNLIKELY(
                (res = bytestream_produce_data(
                    &req->ctx->out,
                    req->ctx->fp)) != 0)) {
            res = MNFCGI_IO_ERROR;
        }
    } else {
        res = MNFCGI_REQUEST_COMPLETED;
    }

    bytestream_rewind(&req->ctx->in);
    bytestream_rewind(&req->ctx->out);
    req->flags.complete = 1;

    return res;
}


int
mnfcgi_render_stdout(mnfcgi_request_t *req,
                     mnfcgi_renderer_t render,
                     void *udata)
{
    int res;
    mnfcgi_record_t *rec;

    //CTRACE("BBB");
    if (req->flags.complete) {
        return MNFCGI_REQUEST_COMPLETED;
    }

    res = 0;
    if (MRKUNLIKELY(
            (rec =
             mnfcgi_record_new(MNFCGI_STDOUT)) == NULL)) {
        res = MNFCGI_RENDER_STDOUT + 1;
        goto end;
    }
    assert(req->begin_request != NULL);
    //CTRACE("rid=%hd", req->begin_request->header.rid);
    rec->header.rid = req->begin_request->header.rid;
    rec->_stdout.render = render;
    rec->_stdout.udata = udata;

    if (MRKUNLIKELY((res = mnfcgi_render(&req->ctx->out,
                                  rec,
                                  req)) != 0)) {
        goto end;
    }

end:
    mnfcgi_record_destroy(&rec);
    return res;

}


static void
_mnfcgi_handle_socket(mnfcgi_ctx_t *ctx)
{
#ifdef TRRET_DEBUG
    CTRACE("started serving request at fd %d", ctx->fd);
#endif
    while (true) {
        mnfcgi_record_t *rec;
        mnhash_item_t *hit;
        mnhash_iter_t it;
        mnfcgi_request_t *req;

        if (MRKUNLIKELY((rec = mnfcgi_parse(&ctx->in, ctx->fp)) == NULL)) {
            goto err;
        }

        //CTRACE("handling type %s", MNFCGI_TYPE_STR(rec->header.type));

        switch (rec->header.type) {
        case MNFCGI_BEGIN_REQUEST:
            {
                mnfcgi_begin_request_t *tmp;

                tmp = (mnfcgi_begin_request_t *)rec;

                if (tmp->role != MNFCGI_RESPONDER) {
                    CTRACE("role not supported %hd", tmp->role);
                    if (MRKUNLIKELY(mnfcgi_render_end_request(ctx,
                                                  rec,
                                                  MNFCGI_UNKNOWN_ROLE,
                                                  0) != 0)) {
                        goto err;
                    }
                    if (MRKUNLIKELY(
                        bytestream_produce_data(&ctx->out, ctx->fp) != 0)) {
                        goto err;
                    }
                    mnfcgi_record_destroy(&rec);

                } else {
                    if (MRKLIKELY((hit = hash_get_item(&ctx->requests,
                            (void *)(uintptr_t)rec->header.rid)) == NULL)) {
                        req = mnfcgi_request_new();
                        req->ctx = ctx;
                        req->begin_request = rec;

                        if (ctx->config->begin_request_parse != NULL) {
                            ssize_t nparsed;

                            if (MRKUNLIKELY(
                                    (nparsed =
                                     ctx->config->begin_request_parse(
                                        rec, &ctx->in, req)) < 0)) {
                                CTRACE("user parser returned %ld", nparsed);

                                (void)mnfcgi_abort_request(
                                        req, (uint32_t)(0 - nparsed));

                                mnfcgi_request_destroy(&req);

                                if (MRKUNLIKELY(
                                    bytestream_produce_data(&ctx->out,
                                                            ctx->fp) != 0)) {
                                    goto err;
                                }
                                /* cannot pass down, request is destroyed */
                                continue;
                            }
                        }

                        if (!req->flags.complete) {
                            hash_set_item(&ctx->requests,
                                          (void *)(uintptr_t)rec->header.rid,
                                          req);
                        } else {
                            mnfcgi_request_destroy(&req);
                        }

                    } else {
                        CTRACE("double request %hd", rec->header.rid);
                        /*
                         * XXX delete this request ?
                         */
                        if (mnfcgi_render_end_request(ctx,
                                                      rec,
                                                      MNFCGI_REQUEST_COMPLETE,
                                                      0) != 0) {
                            goto err;
                        }
                        if (MRKUNLIKELY(
                            bytestream_produce_data(&ctx->out, ctx->fp) != 0)) {
                            goto err;
                        }
                        mnfcgi_record_destroy(&rec);
                    }
                }
            }
            break;

        case MNFCGI_ABORT_REQUEST:
            {
                if (MRKUNLIKELY((hit = hash_get_item(&ctx->requests,
                        (void *)(uintptr_t)rec->header.rid)) == NULL)) {
                    CTRACE("no such request %hd", rec->header.rid);
                    if (mnfcgi_render_end_request(ctx,
                                                  rec,
                                                  MNFCGI_REQUEST_COMPLETE,
                                                  0) != 0) {
                        goto err;
                    }

                } else {
                    req = hit->value;

                    (void)mnfcgi_abort_request(req, 0);
                    hash_delete_pair(&ctx->requests, hit);
                }

                if (MRKUNLIKELY(
                    bytestream_produce_data(&ctx->out,
                                            ctx->fp) != 0)) {
                    goto err;
                }
                mnfcgi_record_destroy(&rec);
            }
            break;


        case MNFCGI_PARAMS:
            {
                if (MRKUNLIKELY((hit = hash_get_item(&ctx->requests,
                        (void *)(uintptr_t)rec->header.rid)) == NULL)) {
                    CTRACE("no such request %hd", rec->header.rid);
                    if (mnfcgi_render_end_request(ctx,
                                                  rec,
                                                  MNFCGI_REQUEST_COMPLETE,
                                                  0) != 0) {
                        goto err;
                    }
                    if (MRKUNLIKELY(
                        bytestream_produce_data(&ctx->out,
                                                ctx->fp) != 0)) {
                        goto err;
                    }
                    mnfcgi_record_destroy(&rec);

                } else {
                    mnfcgi_header_t *h;

                    req = hit->value;
                    h = (mnfcgi_header_t *)rec;
                    STQUEUE_ENQUEUE(&req->params, link, h);

                    if (ctx->config->params_parse != NULL) {
                        ssize_t nparsed;
                        if ((nparsed = ctx->config->params_parse(rec,
                                                        &ctx->in,
                                                        req)) < 0) {
                            CTRACE("user parser returned %ld", nparsed);

                            (void)mnfcgi_abort_request(
                                    req, (uint32_t)(0 - nparsed));

                            hash_delete_pair(&ctx->requests, hit);

                            if (MRKUNLIKELY(
                                bytestream_produce_data(&ctx->out,
                                                        ctx->fp) != 0)) {
                                goto err;
                            }
                            /*
                             * don't mnfcgi_record_destroy(), since it's
                             * already in the queue
                             */

                            /* cannot pass down, request is destroyed */
                            continue;
                        }
                    }

                    if (req->flags.complete) {
                        hash_delete_pair(&ctx->requests, hit);
                    }
                }
            }

            break;

        case MNFCGI_STDIN:
            {
                if (MRKUNLIKELY((hit = hash_get_item(&ctx->requests,
                        (void *)(uintptr_t)
                        rec->header.rid)) == NULL)) {
                    CTRACE("no such request %hd", rec->header.rid);
                    if (mnfcgi_render_end_request(ctx,
                                                  rec,
                                                  MNFCGI_REQUEST_COMPLETE,
                                                  0) != 0) {
                        goto err;
                    }
                    if (MRKUNLIKELY(
                        bytestream_produce_data(&ctx->out,
                                                ctx->fp) != 0)) {
                        goto err;
                    }
                    mnfcgi_record_destroy(&rec);

                } else {
                    mnfcgi_header_t *h;

                    req = hit->value;
                    h = (mnfcgi_header_t *)rec;
                    STQUEUE_ENQUEUE(&req->_stdin, link, h);

                    if (ctx->config->stdin_parse != NULL) {
                        ssize_t nparsed;
                        if ((nparsed = ctx->config->stdin_parse(rec,
                                                        &ctx->in,
                                                        req)) < 0) {
                            CTRACE("user parser returned %ld", nparsed);

                            (void)mnfcgi_abort_request(
                                    req, (uint32_t)(0 - nparsed));

                            hash_delete_pair(&ctx->requests, hit);

                            /*
                             * don't mnfcgi_record_destroy(), since it's
                             * already in the queue
                             */
                        }
                    }

                    if (req->flags.complete) {
                        hash_delete_pair(&ctx->requests, hit);
                    }
                }
            }
            break;

        case MNFCGI_DATA:
            {
                if (MRKUNLIKELY((hit = hash_get_item(&ctx->requests,
                        (void *)(uintptr_t)
                        rec->header.rid)) == NULL)) {
                    CTRACE("no such request %hd", rec->header.rid);
                    if (mnfcgi_render_end_request(ctx,
                                                  rec,
                                                  MNFCGI_REQUEST_COMPLETE,
                                                  0) != 0) {
                        goto err;
                    }
                    if (MRKUNLIKELY(
                        bytestream_produce_data(&ctx->out,
                                                ctx->fp) != 0)) {
                        goto err;
                    }
                    mnfcgi_record_destroy(&rec);

                } else {
                    mnfcgi_header_t *h;

                    req = hit->value;
                    h = (mnfcgi_header_t *)rec;
                    STQUEUE_ENQUEUE(&req->data, link, h);

                    if (ctx->config->data_parse != NULL) {
                        ssize_t nparsed;
                        if ((nparsed = ctx->config->data_parse(rec,
                                                        &ctx->in,
                                                        req)) < 0) {
                            CTRACE("user parser returned %ld", nparsed);

                            (void)mnfcgi_abort_request(
                                    req, (uint32_t)(0 - nparsed));

                            hash_delete_pair(&ctx->requests, hit);
                            /*
                             * don't mnfcgi_record_destroy(), since it's
                             * already in the queue
                             */
                        }
                    }

                    if (req->flags.complete) {
                        hash_delete_pair(&ctx->requests, hit);
                    }
                }
            }
            break;

        case MNFCGI_GET_VALUES:
            {
                mnfcgi_get_values_t *tmp;
                mnbytes_t *value;

                tmp = (mnfcgi_get_values_t *)rec;
                if ((hit = hash_get_item(&tmp->values,
                                         &_MNFCGI_MAX_CONNS)) != NULL) {
                    value = bytes_printf("%d", ctx->config->max_conn);
                    hit->value = value;
                    BYTES_INCREF(value);
                }
                if ((hit = hash_get_item(&tmp->values,
                                         &_MNFCGI_MAX_REQS)) != NULL) {
                    value = bytes_printf("%d", ctx->config->max_req);
                    hit->value = value;
                    BYTES_INCREF(value);
                }
                if ((hit = hash_get_item(&tmp->values,
                                         &_MNFCGI_MPXS_CONNS)) != NULL) {
                    value = bytes_new_from_str("1");
                    hit->value = value;
                    BYTES_INCREF(value);
                }
                rec->header.type = MNFCGI_GET_VALUES_RESULT;
                if (MRKUNLIKELY(
                        mnfcgi_render(&ctx->out, rec, NULL) != 0)) {
                    goto err;
                }

                mnfcgi_record_destroy(&rec);
            }

            break;

        default:
            {
                mnfcgi_record_t *response;

                if (MRKUNLIKELY(
                        (response =
                         mnfcgi_record_new(MNFCGI_UNKNOWN_TYPE)) == NULL)) {
                    goto err;
                }

                response->unknown_type.type = rec->header.type;
                if (MRKUNLIKELY(mnfcgi_render(&ctx->out,
                                              response,
                                              NULL) != 0)) {
                    mnfcgi_record_destroy(&response);
                    goto err;
                }

                mnfcgi_record_destroy(&response);
                if (MRKUNLIKELY(
                        bytestream_produce_data(&ctx->out, ctx->fp) != 0)) {
                    goto err;
                }
                mnfcgi_record_destroy(&rec);
            }

            break;
        }

        continue;

err:
        bytestream_rewind(&ctx->in);
        bytestream_rewind(&ctx->out);
        mnfcgi_record_destroy(&rec);
        /* set requests complete */
        for (hit = hash_first(&ctx->requests, &it);
             hit != NULL;
             hit = hash_next(&ctx->requests, &it)) {
            req = hit->value;
            req->flags.complete = 1;
        }

        break;
    }

#ifdef TRRET_DEBUG
    CTRACE("finished serving request at fd %d", ctx->fd);
#endif
}


int
mnfcgi_flush_out(mnfcgi_request_t *req)
{
    int res;

    res = 0;
    if (!req->flags.complete) {
        if (MRKUNLIKELY(
                (res = bytestream_produce_data(
                    &req->ctx->out,
                    req->ctx->fp)) != 0)) {
            res = MNFCGI_IO_ERROR;
        }
        bytestream_rewind(&req->ctx->out);
    } else {
        res = MNFCGI_REQUEST_COMPLETED;
    }
    return res;
}

int
mnfcgi_finalize_request(mnfcgi_request_t *req)
{
    int res;
    mnfcgi_header_t *h;

    if (!req->flags.complete) {
        while ((h = STQUEUE_HEAD(&req->_stdout)) != NULL) {
            mnfcgi_record_t *rec;

            rec = (mnfcgi_record_t *)h;
            if (MRKUNLIKELY(mnfcgi_render(&req->ctx->out, rec, req) != 0)) {
            }
            mnfcgi_record_destroy(&rec);
        }
        /* empty stdout */
        if (mnfcgi_render_empty_stdout(
                    req->ctx, req) != 0) {
        }
        /* end request */
        if (mnfcgi_render_end_request(
                req->ctx,
                req->begin_request,
                MNFCGI_REQUEST_COMPLETE,
                0) != 0) {
        }

        if (MRKUNLIKELY(
                (res = bytestream_produce_data(
                    &req->ctx->out,
                    req->ctx->fp)) != 0)) {
            res = MNFCGI_IO_ERROR;
        }
        req->flags.complete = 1;
    } else {
        while ((h = STQUEUE_HEAD(&req->_stdout)) != NULL) {
            mnfcgi_record_t *rec;

            rec = (mnfcgi_record_t *)h;
            mnfcgi_record_destroy(&rec);
        }
        res = MNFCGI_REQUEST_COMPLETED;
    }

    bytestream_rewind(&req->ctx->in);
    bytestream_rewind(&req->ctx->out);

    return res;
}


ssize_t PRINTFLIKE(2, 3)
mnfcgi_printf(mnbytestream_t *bs, const char *fmt, ...)
{
    ssize_t nwritten;
    va_list ap;

    va_start(ap, fmt);
    nwritten = bytestream_vnprintf(bs, MNFCGI_MAX_PAYLOAD, fmt, ap);
    va_end(ap);
    return nwritten;
}


ssize_t
mnfcgi_payload_size(ssize_t sz)
{
    return sz <= MNFCGI_MAX_PAYLOAD ? sz : BYTESTREAM_NPRINTF_NEEDMORE;
}


ssize_t
mnfcgi_cat(mnbytestream_t *bs, size_t sz, const char *data)
{
    if (sz <= MNFCGI_MAX_PAYLOAD) {
        return bytestream_cat(bs, sz, data);
    }
    return BYTESTREAM_CAT_ERROR;
}



static int
mnfcgi_handle_socket(UNUSED int argc, void **argv)
{
    mnfcgi_ctx_t ctx;
    mnfcgi_config_t *config;
    int fd;

    config = argv[0];
    ++config->stats.nthreads;
    fd = (int)(intptr_t)argv[1];
    mnfcgi_ctx_init(&ctx, config, fd);
    ctx.thread = mrkthr_me();
    _mnfcgi_handle_socket(&ctx);
    ctx.thread = NULL;
    --config->stats.nthreads;
    mnfcgi_ctx_fini(&ctx);
    return 0;
}


int
mnfcgi_serve(mnfcgi_config_t *config)
{
    int res;
    res = 0;

    if ((config->fd = mrkthr_socket_bind(BCDATA(config->host),
                                  BCDATA(config->port),
                                  PF_INET)) == -1) {
        res = MNFCGI_SERVE + 1;
        goto end;
    }

    CTRACE("mnfcgi listening on %s:%s",
           BDATA(config->host),
           BDATA(config->port));

    if (listen(config->fd, config->max_conn) != 0) {
        res = MNFCGI_SERVE + 2;
        goto end;
    }

    while (true) {
        mrkthr_socket_t *sockets;
        off_t sz, i;

        sockets = NULL;
        sz = 0;
        if ((res = mrkthr_accept_all2(config->fd, &sockets, &sz)) != 0) {
            res = MNFCGI_SERVE + 3;
            if (sockets != NULL) {
                free(sockets);
            }
            goto end;
        }

        for (i = 0; i < sz; ++i) {
            mrkthr_ctx_t *thread;
            thread = MRKTHR_SPAWN(NULL,
                                  mnfcgi_handle_socket,
                                  config,
                                  (void *)(intptr_t)(sockets + i)->fd);
            mrkthr_set_name(thread, "sock#%d", (sockets + i)->fd);
        }
        free(sockets);
    }

end:
    return res;
}
