#ifndef MNFCGI_TESTOAUTH_H_DEFINED
#define MNFCGI_TESTOAUTH_H_DEFINED

#include <mrkcommon/bytes.h>
#include <mrkcommon/hash.h>
#include <mrkcommon/heap.h>
#include <mrkcommon/stqueue.h>

#include <mnfcgi_app.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <mrkthr.h>
#include <mrkpq.h>
/*
 * mrkpq_cache
 */


typedef struct _mrkpq_cache_cmd {
    STQUEUE_ENTRY(_mrkpq_cache_cmd, link);
    void *udata;
    int cmd;
} mrkpq_cache_cmd_t;

typedef struct _mrkpq_cache {
    PGconn *conn;
    mnbytes_t *connstr;
    mrkthr_ctx_t *thread;
    mrkthr_cond_t cond;
    STQUEUE(_mrkpq_cache_cmd, qcmd);
    int state;

    /*
     * weak mrkpq_cache_entry_t *
     */
    mnhash_t prepared;

    /*
     * strong mrkpq_cache_entry_t *
     */
    mnheap_t mru;
} mrkpq_cache_t;

typedef struct _mrkpq_cache_entry {
    uint64_t ts;
    mnbytes_t *stmt;
    //int nparams;
    //const char *const *params;
    //mrkpq_result_cb_t cb;
    //void *udata;
} mrkpq_cache_entry_t;


#define MRKPQ_CACHE_WORKER (INT_MIN + 0x00010000)
int mrkpq_cache_exec(mrkpq_cache_t *,
                     mnbytes_t *,
                     int,
                     const char *const *,
                     mrkpq_result_cb_t,
                     void *);
int mrkpq_cache_init(mrkpq_cache_t *, const char *);
void mrkpq_cache_fini(mrkpq_cache_t *);
void mrkpq_cache_signal_error(mrkpq_cache_t *);


/*
 * oauth2 end points
 */
int testoauth_login_get(mnfcgi_request_t *, void *);

/*
 * data access
 */
int testoauth_verify_cred(const char *, const char *, mnbytes_t **);
int testoauth_verify_client_nonsecure(const char *, const char *);


int testoauth_app_init(mnfcgi_app_t *);
int testoauth_begin_request(mnfcgi_request_t *, void *);
int testoauth_params(mnfcgi_request_t *, void *);
int testoauth_stdin(mnfcgi_request_t *, void *);
int testoauth_data(mnfcgi_request_t *, void *);
int testoauth_stdin_end(mnfcgi_request_t *, void *);
int testoauth_end_request(mnfcgi_request_t *, void *);
int testoauth_app_fini(mnfcgi_app_t *);

#ifdef __cplusplus
}
#endif
#endif /* MNFCGI_TESTOAUTH_H_DEFINED */
