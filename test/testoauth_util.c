#include <stdlib.h>
#include <stdbool.h>

#include <mncommon/util.h>
#include "testoauth.h"

#include "diag.h"


static void
mnpq_cache_entry_init(mnpq_cache_entry_t *e)
{
    e->stmt = NULL;
    e->ts = 0;
}


static mnpq_cache_entry_t *
mnpq_cache_entry_new(void)
{
    mnpq_cache_entry_t *res;

    if ((res = malloc(sizeof(mnpq_cache_entry_t))) == NULL) {
        FAIL("malloc");
    }
    mnpq_cache_entry_init(res);
    return res;
}


static void
mnpq_cache_entry_fini(mnpq_cache_entry_t *e)
{
    BYTES_DECREF(&e->stmt);
    e->ts = 0;
}


static void
mnpq_cache_entry_destroy(mnpq_cache_entry_t **e)
{
    if (*e != NULL) {
        mnpq_cache_entry_fini(*e);
        free(*e);
        *e = NULL;
    }
}


static uint64_t
mnpq_cache_entry_hash_prepared(mnpq_cache_entry_t *e)
{
    return bytes_hash(e->stmt);
}


static int
mnpq_cache_entry_cmp_prepared(mnpq_cache_entry_t *a,
                               mnpq_cache_entry_t *b)
{
    return bytes_cmp(a->stmt, b->stmt);
}


static int
mnpq_cache_entry_fini_mru(mnpq_cache_entry_t **e)
{
    mnpq_cache_entry_destroy(e);
    return 0;
}


static int
mnpq_cache_entry_cmp_mru(mnpq_cache_entry_t **a, mnpq_cache_entry_t **b)
{
    assert(*a != NULL && *b != NULL);
    return MNCMP((*a)->ts, (*b)->ts);
}


static int
mnpq_cache_worker(UNUSED int argc, void **argv)
{
    int res = 0;
#define MNPQ_CW_ST_OK 0
#define MNPQ_CW_ST_ECONN 1
    assert(argc == 1);
    mnpq_cache_t *cache = argv[0];

    if ((cache->conn = mnpq_connect_str(
                    BCDATA(cache->connstr))) == NULL) {
        res = MNPQ_CACHE_WORKER + 1;
        goto end;
    }

    mnthr_cond_init(&cache->cond);
    STQUEUE_INIT(&cache->qcmd);
    cache->state = MNPQ_CW_ST_OK;
    while (true) {
        mnpq_cache_cmd_t *cmd;

        if (mnthr_cond_wait(&cache->cond) != 0) {
            break;
        }

        while ((cmd = STQUEUE_HEAD(&cache->qcmd)) != NULL) {
            CTRACE("worker received signal %d", cmd->cmd);
            STQUEUE_DEQUEUE(&cache->qcmd, link);
            STQUEUE_ENTRY_FINI(link, cmd);
            switch (cache->state) {
            case MNPQ_CW_ST_ECONN:
                if (cmd->cmd == MNPQ_CW_ST_ECONN) {
                    /* ignore */
                } else {
                    cache->state = cmd->cmd;
                    /* process */
                }
                break;

            default:
                /* process */
                cache->state = cmd->cmd;
                if (cmd->cmd == MNPQ_CW_ST_ECONN) {
                } else {
                }
                break;
            }

            free(cmd);
            cmd = NULL;
        }
    }
    STQUEUE_FINI(&cache->qcmd);
    mnthr_cond_fini(&cache->cond);

end:
    if (cache->conn != NULL) {
        PQfinish(cache->conn);
        cache->conn = NULL;
    }
    cache->thread = NULL;
    CTRACE("Exiting mnpq cache worker with %08x ...", res);
    MNTHRET(res);
}


static bool
mnpq_cache_can_signal(mnpq_cache_t *cache, UNUSED int state)
{
    /*
     * MNPQ_CW_ST_ECONN once set, blocks further commands until it's
     * cleared
     */
    return cache->state != MNPQ_CW_ST_ECONN;
}


static void
mnpq_cache_post_cmd(mnpq_cache_t *cache, int _cmd)
{
    mnpq_cache_cmd_t *cmd;

    if ((cmd = malloc(sizeof(mnpq_cache_cmd_t))) == NULL) {
        FAIL("malloc");
    }
    STQUEUE_ENTRY_INIT(link, cmd);
    cmd->cmd = _cmd;
    STQUEUE_ENQUEUE(&cache->qcmd, link, cmd);
    mnthr_cond_signal_one(&cache->cond);
}

void
mnpq_cache_signal_error(mnpq_cache_t *cache)
{
    if (!mnpq_cache_can_signal(cache, MNPQ_CW_ST_ECONN)) {
        return;
    }
    mnpq_cache_post_cmd(cache, MNPQ_CW_ST_ECONN);
}


int
mnpq_cache_exec(mnpq_cache_t *cache,
                 mnbytes_t *stmt,
                 int nparams,
                 const char *const *params,
                 mnpq_result_cb_t cb,
                 void *udata)
{
    int res;
    mnhash_item_t *hit;
    mnpq_cache_entry_t *ce;

    res = 0;
    probe.stmt = stmt;
    if ((hit = hash_get_item(&cache->prepared,
                             &(mnpq_cache_entry_t){.stmt = stmt})) == NULL) {
        ce = mnpq_cache_entry_new();
        ce->stmt = stmt;
        BYTES_INCREF(ce->stmt);
        hash_set_item(&cache->prepared, ce, NULL);

        if (mnpq_prepare(cache->conn,
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

    if (mnpq_query_prepared(cache->conn,
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
    if ((hit = hash_get_item(&cache->prepared,
                             &(mnpq_cache_entry_t){.stmt = stmt})) == NULL) {
        FAIL("mnpq_cache_exec");
    }
    hash_delete_pair(&cache->prepared, hit);
    goto end;
}


UNUSED static void
mnpq_cache_reset(mnpq_cache_t *cache)
{
    if (cache->thread != NULL) {
        mnthr_set_interrupt_and_join(cache->thread);
        cache->thread = NULL;
    }
    cache->thread = MNTHR_SPAWN("qcache", mnpq_cache_worker, cache);
}


int
mnpq_cache_init(mnpq_cache_t *cache, const char *connstr)
{
    int res;

    hash_init(&cache->prepared,
              127,
              (hash_hashfn_t)mnpq_cache_entry_hash_prepared,
              (hash_item_comparator_t)mnpq_cache_entry_cmp_prepared,
              NULL);

    heap_init(&cache->mru,
              sizeof(mnpq_cache_entry_t *),
              0,
              NULL /*heap_pointer_null*/ ,
              (array_finalizer_t)mnpq_cache_entry_fini_mru,
              (array_compar_t)mnpq_cache_entry_cmp_mru,
              heap_pointer_swap);

    cache->connstr = bytes_new_from_str(connstr);
    BYTES_INCREF(cache->connstr);
    cache->conn = NULL;
    cache->thread = MNTHR_SPAWN("qcache", mnpq_cache_worker, cache);
    /*
     * a trick to poll the thread once to check if the thread has started.
     */
    if ((res = mnthr_peek(cache->thread, 2)) == MNTHR_WAIT_TIMEOUT) {
        res = 0;
    }
    return res;
}


void
mnpq_cache_fini(mnpq_cache_t *cache)
{
    if (cache->thread != NULL) {
        mnthr_set_interrupt_and_join(cache->thread);
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
