#include <assert.h>

#include <mrkcommon/bytes.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/dumpm.h>

#include <mnfcgi.h>
#include <mnfcgi_app.h>
#include "unittest.h"

#ifndef NDEBUG
const char *_malloc_options = "AJ";
#endif

static int
fini_item(mnbytes_t *key, mnbytes_t *value)
{
    BYTES_DECREF(&key);
    BYTES_DECREF(&value);
    return 0;
}

static int
dump_item(mnbytes_t *key, mnbytes_t *value, UNUSED void *udata)
{
    TRACE("%s=%s", BDATA(key), BDATA(value));
    return 0;
}


static void
test_mrkhttp_parse_qterms(void)
{
    BYTES_ALLOCA(_s0, "qwe=b&asd=2&asd=&sdf=&");
    BYTES_ALLOCA(_s1, "a");
    BYTES_ALLOCA(_s2, "=");
    BYTES_ALLOCA(_s3, "");
    BYTES_ALLOCA(_s4, "&");
    BYTES_ALLOCA(_s5, "&=");
    BYTES_ALLOCA(_s6, "=&");
    BYTES_ALLOCA(_s7, "a=");
    BYTES_ALLOCA(_s8, "=a");
    BYTES_ALLOCA(_s9, "a&");
    BYTES_ALLOCA(_s10, "&a");
    mnhash_t hash;

    hash_init(&hash, 17,
              (hash_hashfn_t)bytes_hash,
              (hash_item_comparator_t)bytes_cmp,
              (hash_item_finalizer_t)fini_item);

    TRACE("s=%s", BDATA(_s0));
    (void)mrkhttp_parse_qterms(_s0, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s1));
    (void)mrkhttp_parse_qterms(_s1, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s2));
    (void)mrkhttp_parse_qterms(_s2, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s3));
    (void)mrkhttp_parse_qterms(_s3, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s4));
    (void)mrkhttp_parse_qterms(_s4, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s5));
    (void)mrkhttp_parse_qterms(_s5, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s6));
    (void)mrkhttp_parse_qterms(_s6, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s7));
    (void)mrkhttp_parse_qterms(_s7, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s8));
    (void)mrkhttp_parse_qterms(_s8, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s9));
    (void)mrkhttp_parse_qterms(_s9, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s10));
    (void)mrkhttp_parse_qterms(_s10, '=', '&', &hash);
    hash_traverse(&hash, (hash_traverser_t)dump_item, NULL);
    hash_fini(&hash);
}


static void
test0(void)
{
    struct {
        long rnd;
        int in;
        int expected;
    } data[] = {
        {0, 0, 0},
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        //TRACE("in=%d expected=%d", CDATA.in, CDATA.expected);
        assert(CDATA.in == CDATA.expected);
    }


}


static int
myhandler(UNUSED mnfcgi_request_t *req, UNUSED void *udata)
{
    return 0;
}



mnbytes_t * _mnfcgi_app_get_allowed_methods(mnfcgi_app_t *app, mnbytes_t *script_name);
static void
test1(void)
{
    UNUSED int res;
    mnfcgi_app_t *app;
    BYTES_ALLOCA(__t0, "/t0");
    BYTES_ALLOCA(__t1, "/t1");
    BYTES_ALLOCA(__t2, "/t2");
    BYTES_ALLOCA(__t3, "/t3");
    BYTES_ALLOCA(__t4, "/t4");
    BYTES_ALLOCA(__t5, "/t5");
    BYTES_ALLOCA(__t6, "/t6");
    BYTES_ALLOCA(__qwe, "/qwe");

    mnfcgi_app_endpoint_table_t t[] = {
        { __t0, {NULL, NULL,}, },
        { __t1, {myhandler, NULL,}, },
        { __t2, {NULL, myhandler,}, },
        { __t3, {myhandler, myhandler, NULL,}, },
        { __t4, {NULL, myhandler, myhandler, NULL,}, },
        { __t5, {NULL, myhandler, myhandler, myhandler, NULL,}, },
        { __t6, {NULL, myhandler, NULL, myhandler, myhandler, NULL,}, },
    };

    struct {
        long rnd;
        mnbytes_t *in;
        const char *out;
    } data[] = {
        {0, __t0, ""},
        {0, __t1, "GET"},
        {0, __t2, "HEAD"},
        {0, __t3, "GET,HEAD"},
        {0, __t4, "HEAD,POST"},
        {0, __t5, "HEAD,POST,PUT"},
        {0, __t6, "HEAD,PUT,DELETE"},
        {0, __qwe, NULL},
    };
    UNITTEST_PROLOG_RAND;

    app = mnfcgi_app_new("localhost", "1234", 1, 1, NULL);
    assert(app != NULL);
    for (i = 0; i < countof(t); ++i) {
        res = mnfcgi_app_register_endpoint(app, &t[i]);
    }

    FOREACHDATA {
        mnbytes_t *m, *expected;
        m = _mnfcgi_app_get_allowed_methods(app, CDATA.in);
        if (CDATA.out != NULL) {
            expected = bytes_new_from_str(CDATA.out);
        } else {
            expected = NULL;
        }
        assert(bytes_cmp_safe(m, expected) == 0);
        BYTES_DECREF(&m);
        BYTES_DECREF(&expected);
    }
    mnfcgi_app_destroy(&app);
}


int
main(void)
{
    test0();
    test_mrkhttp_parse_qterms();
    test1();
    return 0;
}
