#include <assert.h>
#include <arpa/inet.h>

#include <errno.h>

#include <mncommon/bytestream.h>
#include <mncommon/dumpm.h>
#include <mncommon/util.h>
#include <mnthr.h>

#include "mnfcgi_private.h"

#include "diag.h"

static int
params_item_fini(mnbytes_t *key, mnbytes_t *value)
{
    BYTES_DECREF(&key);
    BYTES_DECREF(&value);
    return 0;
}


#define MNFCGI_REC_INITIALIZER(_ty, __a1)              \
{                                                      \
    _ty *tmp;                                          \
    if (MNUNLIKELY((tmp =                             \
                malloc(sizeof(_ty))) == NULL)) {       \
        FAIL("malloc");                                \
    }                                                  \
    __a1                                               \
    rec = (mnfcgi_record_t *)tmp;                      \
    STQUEUE_ENTRY_INIT(link, &rec->header);            \
    rec->header.version = MNFCGI_VERSION;              \
    rec->header.type = ty;                             \
    rec->header.rid = 0;                               \
    rec->header.rsz = 0;                               \
    rec->header.psz = 0;                               \
    rec->header.reserved = 0;                          \
}                                                      \


mnfcgi_record_t *
mnfcgi_record_new(uint8_t ty)
{
    mnfcgi_record_t *rec;

    rec = NULL;
    switch (ty) {
        case MNFCGI_BEGIN_REQUEST:
            MNFCGI_REC_INITIALIZER(mnfcgi_begin_request_t,
                    tmp->role = 0;
                    tmp->flags = 0;
                    );
            break;

        case MNFCGI_ABORT_REQUEST:
            MNFCGI_REC_INITIALIZER(mnfcgi_abort_request_t,);
            break;

        case MNFCGI_END_REQUEST:
            MNFCGI_REC_INITIALIZER(mnfcgi_end_request_t,
                    tmp->proto_status = 0;
                    tmp->app_status = 0;
                    );
            break;

        case MNFCGI_PARAMS:
            MNFCGI_REC_INITIALIZER(mnfcgi_params_t,
                    hash_init(&tmp->params,
                              127,
                              (hash_hashfn_t)bytes_hash,
                              (hash_item_comparator_t)bytes_cmp,
                              (hash_item_finalizer_t)params_item_fini);
                    );
            break;

        case MNFCGI_STDIN:
            MNFCGI_REC_INITIALIZER(mnfcgi_stdin_t,
                    tmp->br.start = 0;
                    tmp->br.end = 0;
                    tmp->parse = NULL;
                    );
            break;

        case MNFCGI_DATA:
            MNFCGI_REC_INITIALIZER(mnfcgi_data_t,
                    tmp->br.start = 0;
                    tmp->br.end = 0;
                    tmp->parse = NULL;
                    );
            break;

        case MNFCGI_STDOUT:
            MNFCGI_REC_INITIALIZER(mnfcgi_stdout_t,
                    tmp->render = NULL;
                    tmp->udata = NULL;
                    );
            break;

        case MNFCGI_STDERR:
            MNFCGI_REC_INITIALIZER(mnfcgi_stderr_t,
                    tmp->render = NULL;
                    );
            break;

        case MNFCGI_GET_VALUES:
        case MNFCGI_GET_VALUES_RESULT:
            MNFCGI_REC_INITIALIZER(mnfcgi_get_values_t,
                    hash_init(&tmp->values,
                              127,
                              (hash_hashfn_t)bytes_hash,
                              (hash_item_comparator_t)bytes_cmp,
                              (hash_item_finalizer_t)params_item_fini);
                    );
            break;

        case MNFCGI_UNKNOWN_TYPE:
            MNFCGI_REC_INITIALIZER(mnfcgi_unknown_type_t,
                    tmp->type = 0;
                    );
            break;


        default:
            break;
    }

    return rec;
}


void
mnfcgi_record_destroy(mnfcgi_record_t **rec)
{
    if (*rec != NULL) {
        STQUEUE_ENTRY_FINI(link, &(*rec)->header);

        switch ((*rec)->header.type) {
        case MNFCGI_PARAMS:
            {
                mnfcgi_params_t *tmp = (mnfcgi_params_t *)(*rec);
                hash_fini(&tmp->params);
            }
            break;

        case MNFCGI_GET_VALUES:
        case MNFCGI_GET_VALUES_RESULT:
            {
                mnfcgi_get_values_t *tmp = (mnfcgi_get_values_t *)(*rec);
                hash_fini(&tmp->values);
            }
            break;

        default:
            break;
        }

        free(*rec);
        *rec = NULL;
    }
}


#define MNFCGI_PARSE_CHAR(bs, idx) (uint8_t)SPDATA(bs)[idx]

#define MNFCGI_PARSE_SHORT(bs, idx)     \
    ntohs(*((uint16_t *)(SPDATA(bs) + idx)))

#define MNFCGI_PARSE_INT(bs, idx)       \
    (ntohl(*((uint32_t *)(SPDATA(bs) + idx))) & 0x7fffffff)


#define MNFCGI_RENDER_CHAR(bs, v) SCATC(bs, v)

#define MNFCGI_RENDER_SHORT(bs, v) SCATI(bs, uint16_t, htons(v))


#define MNFCGI_RENDER_INT(bs, v)                       \
do {                                                   \
    if ((v) < 0x7f) {                                  \
        SCATC(bs, v);                                  \
    } else {                                           \
        SCATI(bs, uint32_t, htonl((v) | 0x80000000));  \
    }                                                  \
} while (0)                                            \


#define MNFCGI_RENDER_INT4(bs, v) SCATI(bs, uint32_t, htonl(v))


#define MNFCGI_PARSE_KVP_FNONULL 0x01
static ssize_t
mnfcgi_parse_kvp(UNUSED mnfcgi_record_t *rec,
                 mnbytestream_t *bs,
                 mnbytes_t **key,
                 mnbytes_t **value,
                 int flags)
{
    int idx;
    uint8_t probe;
    int ksz, vsz;

    idx = 0;
    probe = MNFCGI_PARSE_CHAR(bs, idx);
    if (!(probe & 0x800)) {
        ksz = probe;
        idx += 1;

    } else {
        ksz = MNFCGI_PARSE_INT(bs, idx);
        idx += 4;
    }

    probe = MNFCGI_PARSE_CHAR(bs, idx);
    if (!(probe & 0x800)) {
        vsz = probe;
        idx += 1;

    } else {
        vsz = MNFCGI_PARSE_INT(bs, idx);
        idx += 4;
    }

    if (ksz > 0) {
        *key = bytes_new_from_str_len(SDATA(bs, SPOS(bs) + idx), ksz);

    } else {
        if (flags & MNFCGI_PARSE_KVP_FNONULL) {
            *key = bytes_new(1);
            BDATA(*key)[0] = '\0';
        } else {
            *key = NULL;
        }
    }

    if (vsz > 0) {
        *value = bytes_new_from_str_len(SDATA(bs, SPOS(bs) + idx + ksz), vsz);

    } else {
        if (flags & MNFCGI_PARSE_KVP_FNONULL) {
            *value = bytes_new(1);
            BDATA(*value)[0] = '\0';
        } else {
            *value = NULL;
        }
    }

    return idx + ksz + vsz;
}


static ssize_t
mnfcgi_parse_payload(mnbytestream_t *bs, mnfcgi_record_t *rec)
{
    ssize_t nread;

    nread = 0;
    switch (rec->header.type) {
    case MNFCGI_BEGIN_REQUEST:
        {
            mnfcgi_begin_request_t *tmp = (mnfcgi_begin_request_t *)rec;

            tmp->role = MNFCGI_PARSE_SHORT(bs, 0);
            tmp->flags = MNFCGI_PARSE_CHAR(bs, 2);
            nread = 8;
            SADVANCEPOS(bs, 8);
        }
        break;

    case MNFCGI_ABORT_REQUEST:
        break;

    case MNFCGI_END_REQUEST:
        /* not implemented */
        nread = -1;
        break;

    case MNFCGI_PARAMS:
        {
            mnfcgi_params_t *tmp = (mnfcgi_params_t *)rec;
            mnbytes_t *key, *value;

            for (nread = 0, key = NULL, value = NULL;
                 nread < rec->header.rsz;) {
                ssize_t n;

                if (MNUNLIKELY(
                        (n = mnfcgi_parse_kvp(
                                rec,
                                bs,
                                &key,
                                &value,
                                MNFCGI_PARSE_KVP_FNONULL)) == -1)) {
                    nread = -1;
                    break;
                }

                //CTRACE("n=%ld nread=%ld param %s=%s",
                //       n, nread, BDATASAFE(key), BDATASAFE(value));

                if (key != NULL) {
                    mnbytes_t *oldkey, *oldvalue;
                    BYTES_INCREF(key);
                    BYTES_INCREF(value);
                    hash_set_item_uniq(&tmp->params,
                                       key,
                                       value,
                                       (void **)&oldkey,
                                       (void **)&oldvalue);

                } else {
                    CTRACE("ignoring null key for value: %s",
                           BDATASAFE(value));
                    BYTES_DECREF(&key);
                    BYTES_DECREF(&value);
                }

                nread += n;
                SADVANCEPOS(bs, n);
            }
        }
        break;

    case MNFCGI_STDIN:
        rec->_stdin.br.start = SPOS(bs);
        SADVANCEPOS(bs, rec->header.rsz);
        rec->_stdin.br.end = SPOS(bs);
        nread = rec->header.rsz;
        break;

    case MNFCGI_DATA:
        rec->data.br.start = SPOS(bs);
        SADVANCEPOS(bs, rec->header.rsz);
        rec->data.br.end = SPOS(bs);
        nread = rec->header.rsz;
        break;

    case MNFCGI_STDOUT:
    case MNFCGI_STDERR:
        /* not implemented */
        nread = -1;
        break;

    case MNFCGI_GET_VALUES:
    case MNFCGI_GET_VALUES_RESULT:
        {
            mnfcgi_get_values_t *tmp = (mnfcgi_get_values_t *)rec;
            mnbytes_t *key, *value;

            for (nread = 0, key = NULL, value = NULL;
                 nread < rec->header.rsz;) {
                ssize_t n;

                if (MNUNLIKELY(
                        (n = mnfcgi_parse_kvp(
                                rec,
                                bs,
                                &key,
                                &value,
                                0)) == -1)) {
                    nread = -1;
                    break;
                }

                //CTRACE("value %s=%s",
                //       BDATASAFE(key), BDATASAFE(value));

                if (MNLIKELY(key != NULL)) {
                    mnbytes_t *oldkey, *oldvalue;
                    BYTES_INCREF(key);
                    BYTES_INCREF(value);
                    hash_set_item_uniq(&tmp->values,
                                       key,
                                       value,
                                       (void **)&oldkey,
                                       (void **)&oldvalue);

                } else {
                    CTRACE("skipping kvp %s:%s",
                           BDATASAFE(key),
                           BDATASAFE(value));
                    BYTES_DECREF(&key);
                    BYTES_DECREF(&value);
                }

                nread += n;
                SADVANCEPOS(bs, n);
            }
        }
        break;

    default:
        FAIL("mnfcgi_parse_payload");
    }

    return nread;
}


static ssize_t
mnfcgi_parse_padding(mnbytestream_t *bs, mnfcgi_record_t *rec)
{
    SADVANCEPOS(bs, rec->header.psz);
    return rec->header.psz;
}


mnfcgi_record_t *
mnfcgi_parse(mnbytestream_t *bs, void *fd)
{
    int rv;
    mnfcgi_record_t *res;
    uint8_t version;
    uint8_t type;
    ssize_t nread;

    res = NULL;

    //CTRACE(">>> SPOS=%ld SAVAIL=%ld", SPOS(bs), SAVAIL(bs));
    while (SAVAIL(bs) < MNFCGI_HEADER_LEN) {
        if ((rv = bytestream_consume_data(bs, fd)) != 0) {
            goto err;
        }
    }
    //CTRACE("<<< SPOS=%ld SAVAIL=%ld", SPOS(bs), SAVAIL(bs));

    /**/
    version = MNFCGI_PARSE_CHAR(bs, 0);
    /**/
    type = MNFCGI_PARSE_CHAR(bs, 1);

    //CTRACE("version=%d type=%s", version, MNFCGI_TYPE_STR(type));

    if ((res = mnfcgi_record_new(type)) == NULL) {
        CTRACE("Unknown type %d version %d", type, version);
        goto err;
    }
    if (version != MNFCGI_VERSION) {
        CTRACE("Unknown version %d", version);
        goto err;
    }

    res->header.version = version;

    /**/
    res->header.rid = MNFCGI_PARSE_SHORT(bs, 2);
    if (res->header.rid == 0) {
        if (!(type == MNFCGI_GET_VALUES ||
              type == MNFCGI_GET_VALUES_RESULT ||
              type == MNFCGI_UNKNOWN_TYPE)) {
            CTRACE("request id zero with type %s",
                   MNFCGI_TYPE_STR(type));
            goto err;
        }
    } else {
        if ((type == MNFCGI_GET_VALUES ||
             type == MNFCGI_GET_VALUES_RESULT ||
             type == MNFCGI_UNKNOWN_TYPE)) {
            CTRACE("request id %d with type %s",
                   res->header.rid,
                   MNFCGI_TYPE_STR(type));
            goto err;
        }
    }
    res->header.rsz = MNFCGI_PARSE_SHORT(bs, 4);
    res->header.psz = MNFCGI_PARSE_CHAR(bs, 6);
    /* ignoring reserved*/

    //CTRACE("ver=%d type=%s rid=%hu rsz=%u psz=%hhd",
    //       version,
    //       MNFCGI_TYPE_STR(type),
    //       res->header.rid,
    //       res->header.rsz,
    //       res->header.psz);

    SADVANCEPOS(bs, MNFCGI_HEADER_LEN);

    //CTRACE(">>> SPOS=%ld SAVAIL=%ld", SPOS(bs), SAVAIL(bs));
    while (SAVAIL(bs) < res->header.rsz) {
        if ((rv = bytestream_consume_data(bs, fd)) != 0) {
            CTRACE("rv=%d errno=%d", rv, errno);
            perror("qwe");
            goto err;
        }
    }
    //CTRACE("<<< SPOS=%ld SAVAIL=%ld", SPOS(bs), SAVAIL(bs));

    if ((nread = mnfcgi_parse_payload(bs, res)) < res->header.rsz) {
        CTRACE("Could not parse payload rsz=%d nread=%ld",
               res->header.rsz,
               nread);
        goto err;
    }

    while (SAVAIL(bs) < res->header.psz) {
        if ((rv = bytestream_consume_data(bs, fd)) != 0) {
            goto err;
        }
    }

    if ((nread = mnfcgi_parse_padding(bs, res)) < res->header.psz) {
        CTRACE("Could not parse padding psz=%hhd nread=%ld",
               res->header.psz,
               nread);
        goto err;
    }


end:
    return res;

err:
    mnfcgi_record_destroy(&res);
    goto end;
}


static ssize_t
mnfcgi_render_padding(mnbytestream_t *bs, mnfcgi_record_t *rec)
{
    uint8_t p[] = {
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
        MNFCGI_PADDING_VALUE,
    };
    bytestream_cat(bs, rec->header.psz, (char *)p);
    return rec->header.psz;
}


static ssize_t
mnfcgi_render_kvp(mnbytestream_t *bs, mnbytes_t *key, mnbytes_t *value)
{
    off_t eod;

    eod = SEOD(bs);
    MNFCGI_RENDER_INT(bs, key->sz - 1);
    MNFCGI_RENDER_INT(bs, value->sz - 1);
    if (MNUNLIKELY(bytestream_cat(bs,
                                   key->sz - 1,
                                   BCDATA(key)) < 0)) {
        return -1;
    }
    if (MNUNLIKELY(bytestream_cat(bs,
                                   value->sz - 1,
                                   BCDATA(value)) < 0)) {
        return -1;
    }
    return eod = SEOD(bs);
}

static ssize_t
mnfcgi_render_payload(mnbytestream_t *bs, mnfcgi_record_t *rec, void *udata)
{
    ssize_t nwritten = 0;

    switch (rec->header.type) {
    case MNFCGI_BEGIN_REQUEST:
        {
            mnfcgi_begin_request_t *tmp = (mnfcgi_begin_request_t *)rec;
            uint8_t p[] = {
                MNFCGI_PADDING_VALUE,
                MNFCGI_PADDING_VALUE,
                MNFCGI_PADDING_VALUE,
                MNFCGI_PADDING_VALUE,
                MNFCGI_PADDING_VALUE,
            };

            MNFCGI_RENDER_SHORT(bs, (tmp->role));
            MNFCGI_RENDER_CHAR(bs, tmp->flags);
            bytestream_cat(bs, sizeof(p), (const char *)p);
            nwritten = 8;
        }
        break;

    case MNFCGI_ABORT_REQUEST:
        break;

    case MNFCGI_END_REQUEST:
        {
            mnfcgi_end_request_t *tmp = (mnfcgi_end_request_t *)rec;
            uint8_t p[] = {
                0,0,0
                //MNFCGI_PADDING_VALUE,
                //MNFCGI_PADDING_VALUE,
                //MNFCGI_PADDING_VALUE,
            };

            //CTRACE("eod1=%ld", SEOD(bs));
            MNFCGI_RENDER_CHAR(bs, tmp->proto_status);
            //D8(SDATA(bs, 0), SEOD(bs));

            //CTRACE("eod0=%ld", SEOD(bs));
            MNFCGI_RENDER_INT4(bs, tmp->app_status);
            //D8(SDATA(bs, 0), SEOD(bs));

            //CTRACE("eod2=%ld", SEOD(bs));
            bytestream_cat(bs, sizeof(p), (const char *)p);
            //D8(SDATA(bs, 0), SEOD(bs));
            //CTRACE("eod3=%ld", SEOD(bs));
            nwritten = 8;
        }
        break;

    case MNFCGI_PARAMS:
        /* not implemented */
        nwritten = -1;
        break;

    case MNFCGI_STDOUT:
        {
            mnfcgi_stdout_t *tmp = (mnfcgi_stdout_t *)rec;
            if (tmp->render != NULL) {
                nwritten = tmp->render(rec, bs, udata);
            } else {
                nwritten = 0;
            }
        }
        break;

    case MNFCGI_STDERR:
        {
            mnfcgi_stderr_t *tmp = (mnfcgi_stderr_t *)rec;
            if (tmp->render != NULL) {
                nwritten = tmp->render(rec, bs, udata);
            } else {
                nwritten = 0;
            }
        }
        break;

    case MNFCGI_GET_VALUES:
    case MNFCGI_GET_VALUES_RESULT:
        {
            mnfcgi_get_values_t *tmp;
            mnhash_iter_t it;
            mnhash_item_t *hit;

            tmp = (mnfcgi_get_values_t *)rec;

            nwritten = 0;
            for (hit = hash_first(&tmp->values, &it);
                 hit != NULL;
                 hit = hash_next(&tmp->values, &it)) {

                mnbytes_t *key, *value;

                key = hit->key;
                value = hit->value;
                if (MNLIKELY(value != NULL)) {
                    if (MNLIKELY(key != NULL)) {
                        ssize_t n;

                        if (MNUNLIKELY(
                                (n = mnfcgi_render_kvp(bs, key, value)) < 0)) {
                            nwritten = -1;
                            break;
                        }
                        nwritten += n;
                    } else {
                        CTRACE("skipping kvp %s:%s",
                               BDATASAFE(key),
                               BDATASAFE(value));
                    }
                } else {
                    CTRACE("skipping kvp %s:%s",
                           BDATASAFE(key),
                           BDATASAFE(value));
                }
            }
        }
        break;

    default:
        break;
    }

    return nwritten;
}


int
mnfcgi_render(mnbytestream_t *bs, mnfcgi_record_t *rec, void *udata)
{
    int res;
    UNUSED ssize_t nwritten;
    ssize_t n;
    off_t eod0, eod;

    nwritten = 0;
    res = 0;

    MNFCGI_RENDER_CHAR(bs, rec->header.version);
    ++nwritten;

    MNFCGI_RENDER_CHAR(bs, rec->header.type);
    ++nwritten;

    MNFCGI_RENDER_SHORT(bs, rec->header.rid);
    nwritten += 2;

    /* placeholder */
    eod0 = SEOD(bs);
    MNFCGI_RENDER_SHORT(bs, 0);
    nwritten += 2;

    /* placeholder */
    MNFCGI_RENDER_SHORT(bs, 0);
    nwritten += 2;

    eod = SEOD(bs);
    if (MNUNLIKELY((n = mnfcgi_render_payload(bs, rec, udata)) < 0)) {
        res = MNFCGI_RENDER_ERROR;
        goto end;

    } else {
#ifndef NDEBUG
        if ((SEOD(bs) - eod) != n) {
            CTRACE("SEOD=%ld seod=%ld n=%ld", SEOD(bs), eod, n);
        }
#endif
        assert((SEOD(bs) - eod) == n);
        nwritten += n;
    }

    if (n > MNFCGI_MAX_PAYLOAD) {
        CTRACE("Payload too lorge: %ld, discarding record.", n);
        res = MNFCGI_PAYLOAD_TOO_LARGE;
        goto end;
    }

    rec->header.rsz = n;
    rec->header.psz = MNFCGI_HEADER_PADDING(rec);

    //CTRACE("rsz=%hd psz=%hhd eod=%ld",
    //       rec->header.rsz,
    //       rec->header.psz,
    //       SEOD(bs));

    eod = SEOD(bs);
    if (MNUNLIKELY((n = mnfcgi_render_padding(bs, rec)) < 0)) {
        res = MNFCGI_RENDER_ERROR;
        goto end;

    } else {
        assert((SEOD(bs) - eod) == n);
        nwritten += n;
    }

    eod = SEOD(bs);
    SEOD(bs) = eod0;
    MNFCGI_RENDER_SHORT(bs, rec->header.rsz);
    MNFCGI_RENDER_CHAR(bs, rec->header.psz);
    MNFCGI_RENDER_CHAR(bs, rec->header.reserved);
    SEOD(bs) = eod;

end:
    return res;
}


void *
mnfcgi_stdout_get_udata(mnfcgi_record_t *rec)
{
    assert(rec->header.type == MNFCGI_STDOUT);
    return rec->_stdout.udata;
}


void *
mnfcgi_stderr_get_udata(mnfcgi_record_t *rec)
{
    assert(rec->header.type == MNFCGI_STDERR);
    return rec->_stderr.udata;
}
