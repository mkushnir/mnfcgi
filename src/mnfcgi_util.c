#include <mrkcommon/bytes.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/dumpm.h>
#include <mrkcommon/util.h>

#include "mnfcgi_private.h"

#include "diag.h"

int
mnfcgi_parse_qterms(mnbytes_t *s,
                    char fdelim,
                    char rdelim,
                    mnhash_t *hash)
{
    int res;
    char *ss = (char *)BDATA(s);
    size_t i0, i1, j;


#define MNFCGI_PARSE_QTERMS_FIND_PAIR()                                \
    for (j = i0; j < i1; ++j) {                                        \
        char cc;                                                       \
        cc = ss[j];                                                    \
        if (cc == fdelim) {                                            \
            break;                                                     \
        }                                                              \
    }                                                                  \
/*                                                                     \
    CTRACE("r ss[%ld]:%c,ss[%ld]:%c,ss[%ld]:%c",                       \
           i0,                                                         \
           ss[i0],                                                     \
           j,                                                          \
           ss[j],                                                      \
           i1,                                                         \
           ss[i1]);                                                    \
 */                                                                    \
    if (j > i0) {                                                      \
        mnbytes_t *key, *value;                                        \
        key = bytes_new_from_str_len(ss + i0, j - i0);                 \
/*                                                                     \
        key = bytes_new(j - i0 + 1);                                   \
        memcpy((char *)BDATA(key), ss + i0, j - i0);                   \
        BDATA(key)[j - i0] = '\0';                                     \
 */                                                                    \
        ++j;                                                           \
        if (i1 >= j) {                                                 \
            value = bytes_new_from_str_len(ss + j, i1 - j);            \
/*                                                                     \
            value = bytes_new(i1 - j + 1);                             \
            memcpy((char *)BDATA(value), ss + j, i1 - j);              \
            BDATA(value)[i1 - j] = '\0';                               \
 */                                                                    \
        } else {                                                       \
            value = bytes_new(1);                                      \
            BDATA(value)[0] = '\0';                                    \
        }                                                              \
        bytes_urldecode(key);                                          \
        bytes_urldecode(value);                                        \
        hash_set_item(hash, key, value);                               \
        BYTES_INCREF(key);                                             \
        BYTES_INCREF(value);                                           \
/*        CTRACE("key=%s value=%s", BDATA(key), BDATA(value)); */      \
    }                                                                  \


    res = 0;
    for (i0 = 0, i1 = 0, j = 0; i1 < BSZ(s); ++i1) {
        char c;
        c = ss[i1];
        if (c == rdelim) {
            MNFCGI_PARSE_QTERMS_FIND_PAIR();
            i0 = i1 + 1;
        }
    }
    --i1;
    MNFCGI_PARSE_QTERMS_FIND_PAIR();
    return res;
}
