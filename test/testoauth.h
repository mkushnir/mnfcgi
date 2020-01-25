#ifndef MNFCGI_TESTOAUTH_H_DEFINED
#define MNFCGI_TESTOAUTH_H_DEFINED

#include <mncommon/bytes.h>
#include <mncommon/hash.h>
#include <mncommon/heap.h>
#include <mncommon/stqueue.h>

#include <mnfcgi_app.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <mnthr.h>
#include <mnpq.h>
/*
 * mnpq_cache
 */


typedef struct _mnpq_cache_cmd {
    STQUEUE_ENTRY(_mnpq_cache_cmd, link);
    void *udata;
    int cmd;
} mnpq_cache_cmd_t;

typedef struct _mnpq_cache {
    PGconn *conn;
    mnbytes_t *connstr;
    mnthr_ctx_t *thread;
    mnthr_cond_t cond;
    STQUEUE(_mnpq_cache_cmd, qcmd);
    int state;

    /*
     * weak mnpq_cache_entry_t *
     */
    mnhash_t prepared;

    /*
     * strong mnpq_cache_entry_t *
     */
    mnheap_t mru;
} mnpq_cache_t;

typedef struct _mnpq_cache_entry {
    uint64_t ts;
    mnbytes_t *stmt;
    //int nparams;
    //const char *const *params;
    //mnpq_result_cb_t cb;
    //void *udata;
} mnpq_cache_entry_t;


#define MNPQ_CACHE_WORKER (INT_MIN + 0x00010000)
int mnpq_cache_exec(mnpq_cache_t *,
                     mnbytes_t *,
                     int,
                     const char *const *,
                     mnpq_result_cb_t,
                     void *);
int mnpq_cache_init(mnpq_cache_t *, const char *);
void mnpq_cache_fini(mnpq_cache_t *);
void mnpq_cache_signal_error(mnpq_cache_t *);


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
