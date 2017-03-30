#include <assert.h>

#include <mrkcommon/bytes.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/dumpm.h>

#include <mnfcgi.h>
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
test_mnfcgi_parse_qterms(void)
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
    (void)mnfcgi_parse_qterms(_s0, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s1));
    (void)mnfcgi_parse_qterms(_s1, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s2));
    (void)mnfcgi_parse_qterms(_s2, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s3));
    (void)mnfcgi_parse_qterms(_s3, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s4));
    (void)mnfcgi_parse_qterms(_s4, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s5));
    (void)mnfcgi_parse_qterms(_s5, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s6));
    (void)mnfcgi_parse_qterms(_s6, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s7));
    (void)mnfcgi_parse_qterms(_s7, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s8));
    (void)mnfcgi_parse_qterms(_s8, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s9));
    (void)mnfcgi_parse_qterms(_s9, '=', '&', &hash);
    TRACE("s=%s", BDATA(_s10));
    (void)mnfcgi_parse_qterms(_s10, '=', '&', &hash);
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

int
main(void)
{
    test0();
    test_mnfcgi_parse_qterms();
    return 0;
}
