#include <stdlib.h>
#include <stdbool.h>

#include <mrkcommon/util.h>
#include "testoauth.h"

#include "diag.h"


static void
mrkpq_cache_entry_init(mrkpq_cache_entry_t *e)
{
    e->stmt = NULL;
    e->ts = 0;
}


static mrkpq_cache_entry_t *
mrkpq_cache_entry_new(void)
{
    mrkpq_cache_entry_t *res;

    if ((res = malloc(sizeof(mrkpq_cache_entry_t))) == NULL) {
        FAIL("malloc");
    }
    mrkpq_cache_entry_init(res);
    return res;
}


static void
mrkpq_cache_entry_fini(mrkpq_cache_entry_t *e)
{
    BYTES_DECREF(&e->stmt);
    e->ts = 0;
}


static void
mrkpq_cache_entry_destroy(mrkpq_cache_entry_t **e)
{
    if (*e != NULL) {
        mrkpq_cache_entry_fini(*e);
        free(*e);
        *e = NULL;
    }
}


static uint64_t
mrkpq_cache_entry_hash_prepared(mrkpq_cache_entry_t *e)
{
    return bytes_hash(e->stmt);
}


static int
mrkpq_cache_entry_cmp_prepared(mrkpq_cache_entry_t *a,
                               mrkpq_cache_entry_t *b)
{
    return bytes_cmp(a->stmt, b->stmt);
}


static int
mrkpq_cache_entry_fini_mru(mrkpq_cache_entry_t **e)
{
    mrkpq_cache_entry_destroy(e);
    return 0;
}


static int
mrkpq_cache_entry_cmp_mru(mrkpq_cache_entry_t **a, mrkpq_cache_entry_t **b)
{
    assert(*a != NULL && *b != NULL);
    return MNCMP((*a)->ts, (*b)->ts);
}


static int
mrkpq_cache_worker(UNUSED int argc, void **argv)
{
    int res = 0;
#define MRKPQ_CW_ST_OK 0
#define MRKPQ_CW_ST_ECONN 1
    assert(argc == 1);
    mrkpq_cache_t *cache = argv[0];

    if ((cache->conn = mrkpq_connect_str(
                    BCDATA(cache->connstr))) == NULL) {
        res = MRKPQ_CACHE_WORKER + 1;
        goto end;
    }

    mrkthr_cond_init(&cache->cond);
    STQUEUE_INIT(&cache->qcmd);
    cache->state = MRKPQ_CW_ST_OK;
    while (true) {
        mrkpq_cache_cmd_t *cmd;

        if (mrkthr_cond_wait(&cache->cond) != 0) {
            break;
        }

        while ((cmd = STQUEUE_HEAD(&cache->qcmd)) != NULL) {
            CTRACE("worker received signal %d", cmd->cmd);
            STQUEUE_DEQUEUE(&cache->qcmd, link);
            STQUEUE_ENTRY_FINI(link, cmd);
            switch (cache->state) {
            case MRKPQ_CW_ST_ECONN:
                if (cmd->cmd == MRKPQ_CW_ST_ECONN) {
                    /* ignore */
                } else {
                    cache->state = cmd->cmd;
                    /* process */
                }
                break;

            default:
                /* process */
                cache->state = cmd->cmd;
                if (cmd->cmd == MRKPQ_CW_ST_ECONN) {
                } else {
                }
                break;
            }

            free(cmd);
            cmd = NULL;
        }
    }
    STQUEUE_FINI(&cache->qcmd);
    mrkthr_cond_fini(&cache->cond);

end:
    if (cache->conn != NULL) {
        PQfinish(cache->conn);
        cache->conn = NULL;
    }
    cache->thread = NULL;
    CTRACE("Exiting mrkpq cache worker with %08x ...", res);
    MRKTHRET(res);
}


static bool
mrkpq_cache_can_signal(mrkpq_cache_t *cache, UNUSED int state)
{
    /*
     * MRKPQ_CW_ST_ECONN once set, blocks further commands until it's
     * cleared
     */
    return cache->state != MRKPQ_CW_ST_ECONN;
}


static void
mrkpq_cache_post_cmd(mrkpq_cache_t *cache, int _cmd)
{
    mrkpq_cache_cmd_t *cmd;

    if ((cmd = malloc(sizeof(mrkpq_cache_cmd_t))) == NULL) {
        FAIL("malloc");
    }
    STQUEUE_ENTRY_INIT(link, cmd);
    cmd->cmd = _cmd;
    STQUEUE_ENQUEUE(&cache->qcmd, link, cmd);
    mrkthr_cond_signal_one(&cache->cond);
}

void
mrkpq_cache_signal_error(mrkpq_cache_t *cache)
{
    if (!mrkpq_cache_can_signal(cache, MRKPQ_CW_ST_ECONN)) {
        return;
    }
    mrkpq_cache_post_cmd(cache, MRKPQ_CW_ST_ECONN);
}


int
mrkpq_cache_exec(mrkpq_cache_t *cache,
                 mnbytes_t *stmt,
                 int nparams,
                 const char *const *params,
                 mrkpq_result_cb_t cb,
                 void *udata)
{
    int res;
    mnhash_item_t *hit;
    mrkpq_cache_entry_t *ce, probe;

    res = 0;
    probe.stmt = stmt;
    if ((hit = hash_get_item(&cache->prepared, &probe)) == NULL) {
        ce = mrkpq_cache_entry_new();
        ce->stmt = stmt;
        BYTES_INCREF(ce->stmt);
        hash_set_item(&cache->prepared, ce, NULL);

        if (mrkpq_prepare(cache->conn,
                          BCDATA(ce->stmt),
                          BCDATA(ce->stmt),
                          nparams,
                          NULL,
                          0,
                          cb,
                          NULL,
                          udata) != 0) {
            res = -1;
            goto err;
        }

    } else {
        ce = hit->key;
    }

    if (mrkpq_query_prepared(cache->conn,
                             BCDATA(ce->stmt),
                             nparams,
                             params,
                             NULL,
                             NULL,
                             0,
                             0,
                             cb,
                             NULL,
                             udata) != 0) {
        res = -1;
        goto err;
    }

end:
    return res;

err:
    if ((hit = hash_get_item(&cache->prepared, &probe)) == NULL) {
        FAIL("mrkpq_cache_exec");
    }
    hash_delete_pair(&cache->prepared, hit);
    goto end;
}


UNUSED static void
mrkpq_cache_reset(mrkpq_cache_t *cache)
{
    if (cache->thread != NULL) {
        mrkthr_set_interrupt_and_join(cache->thread);
        cache->thread = NULL;
    }
    cache->thread = MRKTHR_SPAWN("qcache", mrkpq_cache_worker, cache);
}


int
mrkpq_cache_init(mrkpq_cache_t *cache, const char *connstr)
{
    int res;

    hash_init(&cache->prepared,
              127,
              (hash_hashfn_t)mrkpq_cache_entry_hash_prepared,
              (hash_item_comparator_t)mrkpq_cache_entry_cmp_prepared,
              NULL);

    heap_init(&cache->mru,
              sizeof(mrkpq_cache_entry_t *),
              0,
              NULL /*heap_pointer_null*/ ,
              (array_finalizer_t)mrkpq_cache_entry_fini_mru,
              (array_compar_t)mrkpq_cache_entry_cmp_mru,
              heap_pointer_swap);

    cache->connstr = bytes_new_from_str(connstr);
    BYTES_INCREF(cache->connstr);
    cache->conn = NULL;
    cache->thread = MRKTHR_SPAWN("qcache", mrkpq_cache_worker, cache);
    /*
     * a trick to poll the thread once to check if the thread has started.
     */
    if ((res = mrkthr_peek(cache->thread, 2)) == MRKTHR_WAIT_TIMEOUT) {
        res = 0;
    }
    return res;
}


void
mrkpq_cache_fini(mrkpq_cache_t *cache)
{
    if (cache->thread != NULL) {
        mrkthr_set_interrupt_and_join(cache->thread);
        cache->thread = NULL;
    }
    if (cache->conn != NULL) {
        PQfinish(cache->conn);
        cache->conn = NULL;
    }
    BYTES_DECREF(&cache->connstr);
    hash_fini(&cache->prepared);
    heap_fini(&cache->mru);
}
