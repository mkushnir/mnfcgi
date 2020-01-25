#include <inttypes.h> /* intmax_t */

#include <mncommon/dumpm.h>
#include <mncommon/util.h>

#include <mnthr.h>

#include <mnfcgi_app_private.h>

#include "testoauth.h"
#include "diag.h"

extern mnpq_cache_t cache;

static mnbytes_t _begin = BYTES_INITIALIZER("begin;");
static mnbytes_t _commit = BYTES_INITIALIZER("commit;");
static mnbytes_t _rollback = BYTES_INITIALIZER("rollback;");


static int
null_cb(UNUSED PGconn *conn, UNUSED PGresult *qres, UNUSED void *udata)
{
    return 0;
}

#define MNPQ_CACHE_EXEC5(cache, stmt, qparams, cb, rparams)                   \
    mnpq_cache_exec(cache, stmt, countof(qparams), qparams, cb, rparams)      \


#define MNPQ_CACHE_EXEC4(cache, stmt, cb, rparams)    \
    mnpq_cache_exec(cache, stmt, 0, NULL, cb, rparams)\


#define MNPQ_CACHE_EXEC3(cache, stmt, qparams)                \
    MNPQ_CACHE_EXEC5(cache, stmt, qparams, null_cb, NULL)     \


#define MNPQ_CACHE_EXEC2(cache, stmt)                 \
    MNPQ_CACHE_EXEC4(cache, stmt, null_cb, NULL)      \






static mnbytes_t _stmt1 = BYTES_INITIALIZER(
    "select cred.pkey, cred.value, cred.code0, cred.code1 "
    "from cred, ident "
    "where "
    "cred.ident_pkey = ident.pkey and "
    "cred.status = 'active' and "
    "ident.name = $1;");

static int
stmt1_cb(UNUSED PGconn *conn, PGresult *qres, void *udata)
{
    struct {
        const char *p;
        mnbytes_t *pkey;
        mnbytes_t *code0;
        mnbytes_t *code1;
        mnbytes_t *coden;
    } *params = udata;
    char *p;

    //CTRACE("ntuples=%d", PQntuples(qres));
    //CTRACE("nfields=%d", PQnfields(qres));
    if (PQntuples(qres) > 0) {
        int i;

        for (i = 0; i < PQntuples(qres); ++i) {
            if ((p = PQgetvalue(qres, i, 1)) != NULL) {
                //TRACE("params->p=%s p=%s", params->p, p);
                if (strcmp(params->p, p) == 0) {
                    params->pkey = bytes_new_from_str(PQgetvalue(qres, i, 0));
                    params->code0 = bytes_new_from_str(PQgetvalue(qres, i, 2));
                    params->code1 = bytes_new_from_str(PQgetvalue(qres, i, 3));
                    break;
                }
            }
        }
    }

    return 0;
}


static mnbytes_t _stmt2 = BYTES_INITIALIZER(
    "update cred set "
    "code0 = $1, "
    "code1 = $2 "
    "where pkey = $3;");


int
testoauth_verify_cred(const char *ident, const char *cred, mnbytes_t **code)
{
    int res;
    const char *qp1[] = {
        ident,
    };
    struct {
        const char *p;
        mnbytes_t *pkey;
        mnbytes_t *code0;
        mnbytes_t *code1;
        mnbytes_t *coden;
    } rparams = { .p = cred, };
    const char *qp2[] = {
        NULL, NULL, NULL
    };

    res = -1;
    /*
     * query and check
     */
    if (MNPQ_CACHE_EXEC2(&cache, &_begin) != 0) {
        goto err;
    }

    if (MNPQ_CACHE_EXEC5(
                &cache, &_stmt1, qp1, stmt1_cb, &rparams) != 0) {
        res = -1;
        goto err;
    }

    if (rparams.code0 == NULL) {
        res = -1;
        /* not a db error */
        goto end;
    }

    rparams.coden = bytes_printf("code:%ld", mnthr_get_now_ticks_precise());
    qp2[0] = BCDATA(rparams.coden);
    qp2[1] = BCDATA(rparams.code0);
    qp2[2] = BCDATA(rparams.pkey);


    if (MNPQ_CACHE_EXEC3(
                &cache, &_stmt2, qp2) != 0) {
        res = -1;
        goto err;
    }

    if (MNPQ_CACHE_EXEC2(&cache, &_commit) != 0) {
        res = -1;
        goto err;
    }

    *code = rparams.code0;
    rparams.code0 = NULL;
    BYTES_INCREF(*code);
    res = 0;

end:
    BYTES_DECREF(&rparams.pkey);
    BYTES_DECREF(&rparams.code0);
    BYTES_DECREF(&rparams.code1);
    BYTES_DECREF(&rparams.coden);
    return res;

err:
    if (MNPQ_CACHE_EXEC2(&cache, &_rollback) != 0) {
    }
    mnpq_cache_signal_error(&cache);
    goto end;
}



int
testoauth_verify_client_nonsecure(UNUSED const char *client_id,
                                  UNUSED const char *redirect_uri)
{
    int res;

    res = 0;
    /*
     * select ident.name
     * from client_id, cred, ident, redirect_uri
     * where
     *  client_id.cred_pkey = cred.pkey and
     *  client_id.status = 'active' and
     *  cred.ident_pkey = ident.pkey and
     *  cred.status = 'active' and
     *  ident.pkey = redirect_uri.ident_pkey and
     *  redirect_uri.status = 'active' and
     *  client_id.value = $1 and
     *  redirect_uri.value = $2;
     */

    return res;
}


int
testoauth_verify_client_secure(UNUSED const char *client_id,
                               UNUSED const char *redirect_uri,
                               UNUSED const char *secret)
{
    int res;

    res = 0;
    /*
     * select ident.name, cred.value
     * from client_id, cred, ident, redirect_uri
     * where
     *  client_id.cred_pkey = cred.pkey and
     *  client_id.status = 'active' and
     *  cred.ident_pkey = ident.pkey and
     *  cred.status = 'active' and
     *  ident.pkey = redirect_uri.ident_pkey and
     *  redirect_uri.status = 'active' and
     *  client_id.value = $1 and
     *  redirect_uri.value = $2;
     */

    return res;
}
