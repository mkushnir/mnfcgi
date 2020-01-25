#include <mncommon/dumpm.h>
#include <mncommon/util.h>

#include <mnthr.h>

// TODO: convert to use public interface <mnfcgi.h>
#include <mnfcgi_private.h>

#include "testmy.h"
#include "diag.h"
#include "config.h"

static mnbytes_t _server = BYTES_INITIALIZER("Server");
static mnbytes_t _mn_server = BYTES_INITIALIZER("mn-server");
static mnbytes_t _status = BYTES_INITIALIZER("Status");
static mnbytes_t _x_mn0 = BYTES_INITIALIZER("x-mn-0");
static mnbytes_t _x_mn1 = BYTES_INITIALIZER("x-mn-1");
static mnbytes_t _x_mn2 = BYTES_INITIALIZER("x-mn-2");
static mnbytes_t _x_mn_date = BYTES_INITIALIZER("x-mn-date");
static mnbytes_t _date = BYTES_INITIALIZER("date");
static mnbytes_t _x_mn_long = BYTES_INITIALIZER("x-mn-long");

static ssize_t
mystdout_local_redirect(UNUSED mnfcgi_record_t *rec, mnbytestream_t *bs, UNUSED void *udata)
{
    return mnfcgi_printf(bs, "Location: /api/v2/test\r\n\r\n");
}


static ssize_t
mystdout(mnfcgi_record_t *rec, mnbytestream_t *bs, void *udata)
{
    mnfcgi_request_t *req;
    mnbytes_t *qwe;
    int flags = (int)(intptr_t)rec->_stdout.udata;

    req = udata;
    qwe = req->ctx->config->udata;
    //CTRACE("qwe=%s", BDATASAFE(qwe));
    if (flags == 0) {

        mnfcgi_request_field_addt(req, 0,
                                  &_x_mn_date,
                                  MNTHR_GET_NOW_SEC() + 86400);
        mnfcgi_request_field_addt(req, 0,
                                  &_date,
                                  MNTHR_GET_NOW_SEC() - 86400);
        mnfcgi_request_field_addf(req, 0,
                                  &_server,
                                  "%s/%s", PACKAGE, VERSION);
        mnfcgi_request_field_addf(req, 0,
                                  &_mn_server,
                                  "%s/%s", PACKAGE, VERSION);
        mnfcgi_request_field_addf(req, MNFCGI_FADD_IFNOEXISTS,
                                  &_status,
                                  "%d OK(%s/%d)",
                                  206,
                                  BDATASAFE(qwe),
                                  flags);
        mnfcgi_request_field_addf(req, 0, &_x_mn0, "a=%s", BDATASAFE(qwe));
        mnfcgi_request_field_addf(req,
                                  MNFCGI_FADD_IFNOEXISTS,
                                  &_x_mn1,
                                  "a=%s",
                                  BDATASAFE(qwe));
        mnfcgi_request_field_addf(req,
                                  MNFCGI_FADD_OVERRIDE,
                                  &_x_mn2,
                                  "a=%s",
                                  BDATASAFE(qwe));
        mnfcgi_request_field_addf(req,
                                  MNFCGI_FADD_OVERRIDE,
                                  &_x_mn_long,
                                  "a=%s",
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                                  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
/*
 */
                                  );

        return mnfcgi_printf(
                bs,
                "X-Status: %d OK(%s:%d)\r\n",
                207,
                BDATASAFE(qwe),
                flags);

    } else if (flags == 1) {
        mnfcgi_request_field_addf(req, 0, &_x_mn0, "b=%s", BDATASAFE(qwe));
        mnfcgi_request_field_addf(req,
                                  MNFCGI_FADD_IFNOEXISTS,
                                  &_x_mn1,
                                  "b=%s",
                                  BDATASAFE(qwe));
        mnfcgi_request_field_addf(req,
                                  MNFCGI_FADD_OVERRIDE,
                                  &_x_mn2,
                                  "b=%s",
                                  BDATASAFE(qwe));
        return mnfcgi_printf(bs,
            "X-Status: %d OK(%s:%d)\r\n",
            208,
            BDATASAFE(qwe),
            flags);

    } else if (flags == 2) {
        mnhash_item_t *hit;
        mnhash_iter_t it;
        ssize_t sz;

        sz = 0;
        for (hit = hash_first(&req->headers, &it);
             hit != NULL;
             hit = hash_next(&req->headers, &it)) {
            mnbytes_t *name, *value;
            ssize_t n;

            name = hit->key;
            value = hit->value;
            n = BSZ(name) + BSZ(value) + 16;
            if (n < MNFCGI_MAX_PAYLOAD) {
                sz += mnfcgi_printf(
                        bs,
                        "%s: %s\r\n",
                        BDATA(name),
                        BDATA(value));
            }
        }

        sz += mnfcgi_printf(bs,
                            "\r\n"
                            "<html>\n"
                            "\t<body>\n");
        return mnfcgi_payload_size(sz);

    } else if (flags == 3) {
        return mnfcgi_printf(bs,
            "\t\t<h1>mktest rid=%hd</h1>\n"
            "\t\t<p>qwe: %s (%ld)</p>\n",
            req->begin_request->header.rid,
            BDATASAFE(qwe),
            MNTHR_GET_NOW_SEC());
    } else {
        return mnfcgi_printf(bs,
                             "\t</body>\n"
                             "</html>\n"
                             "<--\n");
    }
}


UNUSED static mnbytes_t _myudata = BYTES_INITIALIZER("my udata");


UNUSED static int
myfinalize(UNUSED int argc, UNUSED void **argv)
{
    mnfcgi_request_t *req;
    UNUSED int res;

    req = argv[0];
    //mnthr_sleep(3500);

    //CTRACE("FATAL %p", req);

    //res = mnfcgi_render_stdout(req, mystdout, NULL);
    //(void)mnfcgi_flush_out(req);
    //(void)mnfcgi_finalize_request(req);
    return 0;
}

ssize_t
testmy_stdin(mnfcgi_record_t *rec, UNUSED mnbytestream_t *bs, void *udata)
{
    mnfcgi_request_t *req = udata;
    //CTRACE(">>> stdin");

    if (rec->header.rsz == 0) {
        int i;
        int res;
        mnbytes_t *request_uri;

        //MNTHR_SPAWN("fin", myfinalize, req);
        //mnthr_sleep(5000);

        request_uri = mnfcgi_request_get_param(req, BYTES_REF("REQUEST_URI"));
        res = 0;
        if (request_uri != NULL &&
                bytes_startswith(request_uri, BYTES_REF("/api/v1"))) {
            res = mnfcgi_render_stdout(req, mystdout_local_redirect, NULL);
        } else {
            res = mnfcgi_render_stdout(req, mystdout, NULL);
            for (i = 0; i < 5; ++i) {
                //mnthr_sleep(5000);
                res = mnfcgi_render_stdout(req,
                                           mystdout,
                                           (void *)(intptr_t)(i + 1));
                (void)mnfcgi_flush_out(req);
            }
            res = mnfcgi_render_stdout(req, mystdout, (void *)(intptr_t)20);
        }

        (void)mnfcgi_finalize_request(req);
    } else {
        //CTRACE("stdin rec %ld", rec->_stdin.br.end - rec->_stdin.br.start);
        //D8(SDATA(bs, rec->_stdin.br.start),
        //   rec->_stdin.br.end - rec->_stdin.br.start);
    }
    //CTRACE("<<< stdin %ld", (ssize_t)rec->header.rsz);
    return rec->header.rsz;
}


ssize_t
testmy_data(mnfcgi_record_t *rec, mnbytestream_t *bs, void *udata)
{
    mnfcgi_request_t *req = udata;
    //CTRACE(">>> data");
    D8(SDATA(bs, rec->data.br.start), rec->data.br.end - rec->data.br.start);
    //CTRACE("<<< data");

    if (rec->header.rsz == 0) {
        int res;

        res = mnfcgi_render_stdout(req, mystdout, NULL);
        mnfcgi_flush_out(req);
        mnfcgi_finalize_request(req);
    }
    return rec->header.rsz;
}


static int
myparams_cb(UNUSED mnbytes_t *key, UNUSED mnbytes_t *value, UNUSED void *udata)
{
    //CTRACE("%s=%s", BDATASAFE(key), BDATASAFE(value));
    return 0;
}


ssize_t
testmy_params(mnfcgi_record_t *rec, UNUSED mnbytestream_t *bs, void *udata)
{
    mnfcgi_request_t *req = udata;
    (void)hash_traverse(&rec->params.params, (hash_traverser_t)myparams_cb, req);
    return rec->header.rsz;
}



