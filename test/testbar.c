#include <err.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>

#ifdef HAVE_MALLOC_H
#   include <malloc.h>
#endif

#include <mncommon/dumpm.h>
#include <mncommon/util.h>

#include <mnthr.h>

// TODO: convert to use public interface <mnfcgi.h>
#include <mnfcgi_app.h>

#include "diag.h"
#include "config.h"

#include "testoauth.h"
#include "testmy.h"

#ifndef NDEBUG
const char *_malloc_options = "AJ";
#endif

static int develop = 0;
static int check_config = 0;
#define BAR_DEFAULT_CONFIG_FILE "/usr/local/etc/bar.conf"
static char *configfile = NULL;
#define BAR_DEFAULT_HOST "localhost"
static char *host = NULL;
#define BAR_DEFAULT_PORT "9000"
static char *port = NULL;
#define BAR_DEFAULT_MAX_CONN 10
#define BAR_CONN_LIMIT 1024
static int max_conn = BAR_DEFAULT_MAX_CONN;
#define BAR_DEFAULT_MAX_REQ 10
#define BAR_REQ_LIMIT 1024
static int max_req = BAR_DEFAULT_MAX_REQ;
#define BAR_DEFAULT_APP "bar"
static char *app = NULL;


static struct option optinfo[] = {
#define BAR_OPT_FILE 0
    {"file", required_argument, NULL, 'f'},
#define BAR_OPT_HELP 1
    {"help", no_argument, NULL, 'h'},
#define BAR_OPT_VERSION 2
    {"version", no_argument, NULL, 'V'},
#define BAR_OPT_DEVELOP 3
    {"develop", no_argument, &develop, 1},
#define BAR_OPT_CHECK_CONFIG 4
    {"check-config", no_argument, &check_config, 1},
#define BAR_OPT_PRINT_CONFIG 5
    {"print-config", required_argument, NULL, 0},
#define BAR_OPT_HOST 6
    {"host", required_argument, NULL, 'H'},
#define BAR_OPT_PORT 7
    {"port", required_argument, NULL, 'P'},
#define BAR_OPT_MAX_CONN 8
    {"max-conn", required_argument, NULL, 'm'},
#define BAR_OPT_MAX_REQ 9
    {"max-req", required_argument, NULL, 'r'},
#define BAR_OPT_APP 10
    {"app", required_argument, NULL, 'a'},
    {NULL, 0, NULL, 0},
};


/*
 * Run-time contxt.
 */
bool shutting_down = false;
bool sigshutdown_sent = false;

static mnfcgi_app_t *fcgi_app = NULL;


static void
usage(char *p)
{
    printf("Usage: %s OPTIONS\n"
        "\n"
        "Options:\n"
        "  --help|-h                    Show this message and exit.\n"
        "  --file=FPATH|-f FPATH        Configuration file (default\n"
        "                               %s).\n"
        "  --version|-V                 Print version and exit.\n"
        "  --develop                    Run in develop mode.\n"
        "  --check-config               Check config and exit.\n"
        "  --print-config=PREFIX        multiple times.\n"
        "                               Passing 'all' will show all\n"
        "                               configuration.\n"
        "  --host|-H                    Address to listen on (default %s).\n"
        "  --port|-P                    Port to listen on (default %s).\n"
        "  --max-conn=NUM|-m NUM        Maximum concurrent connections,\n"
        "                               (default %d).\n"
        "  --max-req=NUM|-r NUM         Maximum concurrent requests,\n"
        "                               (default %d).\n"
        "  --app=NAME|-a NAME           Application to run, (default %s)"
        ""
        "\n",
        basename(p),
        BAR_DEFAULT_CONFIG_FILE,
        BAR_DEFAULT_HOST,
        BAR_DEFAULT_PORT,
        BAR_DEFAULT_MAX_CONN,
        BAR_DEFAULT_MAX_REQ,
        BAR_DEFAULT_APP);
}


#ifndef SIGINFO
UNUSED
#endif
static void
barinfo(UNUSED int sig)
{
    mnthr_dump_all_ctxes();
}


static int
sigshutdown(UNUSED int argc, UNUSED void **argv)
{
    if (!shutting_down) {
        if (!sigshutdown_sent) {
            mnthr_shutdown();

            /*
             * At this point mnthr_loop() should get ready to terminate in
             * main(), and let it exit naturally.
             */
        } else {
            ++sigshutdown_sent;
        }
    } else {
        exit(0);
    }
    return 0;
}


static void
barterm(UNUSED int sig)
{
    (void)MNTHR_SPAWN_SIG("sigshutdown", sigshutdown);
}


static int
testconfig(void)
{
    return 0;
}

static void
initall(void)
{
}


static int
configure(UNUSED const char *configfile)
{
    return 0;
}


static int
run1(UNUSED int argc, UNUSED void **argv)
{
    int res;

    res = 0;
    if (strcmp(app, "oauth2") == 0) {
        mnfcgi_app_callback_table_t t = {
            .init_app = testoauth_app_init,
            .begin_request = testoauth_begin_request, /* NULL? */
            .params_complete = mnfcgi_app_params_complete_select_exact,
            ._stdin = testoauth_stdin, /* NULL? */
            .data = testoauth_data, /* NULL? */
            .stdin_end = testoauth_stdin_end,
            .end_request = testoauth_end_request, /* NULL? */
            .fini_app = testoauth_app_fini,
        };
        fcgi_app = mnfcgi_app_new(host, port, max_conn, max_req, &t);
    } else {
        fcgi_app = mnfcgi_app_new(host, port, max_conn, max_req, NULL);
    }
    if (fcgi_app != NULL) {
        mnfcgi_app_serve(fcgi_app);
    } else {
        res = -1;
    }
    mnfcgi_app_destroy(&fcgi_app);
    return res;
}

static int
run0(UNUSED int argc, UNUSED void **argv)
{
    while (true) {
        UNUSED mnthr_ctx_t *thread;
        int res;

        res = run1(0, NULL);

        //thread = MNTHR_SPAWN("run1", run1);
        //if ((res = mnthr_join(thread)) != 0) {
        //    break;
        //}

        if ((res = mnthr_sleep(1000)) != 0) {
            break;
        }
    }

    return 0;
}


int
main(int argc, char **argv)
{
    int ch;
    int idx;

#ifdef HAVE_MALLOC_H
#   ifndef NDEBUG
    /*
     * malloc options
     */
    if (mallopt(M_CHECK_ACTION, 1) != 1) {
        FAIL("mallopt");
    }
    if (mallopt(M_PERTURB, 0x5a) != 1) {
        FAIL("mallopt");
    }
#   endif
#endif

    /*
     * install signal handlers
     */
    if (signal(SIGINT, barterm) == SIG_ERR) {
        return 1;
    }
    if (signal(SIGTERM, barterm) == SIG_ERR) {
        return 1;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return 1;
    }
#ifdef SIGINFO
    if (signal(SIGINFO, barinfo) == SIG_ERR) {
        return 1;
    }
#endif


    while ((ch = getopt_long(argc, argv, "a:f:hH:m:r:V", optinfo, &idx)) != -1) {
        switch (ch) {
        case 'a':
            app = strdup(optarg);
            break;

        case 'f':
            //CTRACE("read config from %s", optarg);
            configfile = strdup(optarg);
            break;

        case 'H':
            host = strdup(optarg);
            break;

        case 'P':
            port = strdup(optarg);
            break;

        case 'm':
            max_conn = strtoimax(optarg, NULL, 10);
            if (!INB0(1, max_conn, BAR_CONN_LIMIT)) {
                err(1, "Invalid --max-conn|-m option.");
            }
            break;

        case 'r':
            max_req = strtoimax(optarg, NULL, 10);
            if (!INB0(1, max_req, BAR_REQ_LIMIT)) {
                err(1, "Invalid --max-req|-r option.");
            }
            break;

        case 'h':
            usage(argv[0]);
            exit(0);

        case 'V':
            printf("%s\n", PACKAGE_STRING);
            exit(0);

        case 0:
            if (idx == BAR_OPT_PRINT_CONFIG) {
            } else {
                /*
                 * other options
                 */
            }
            break;

        default:
            usage(argv[0]);
            exit(1);
        }
    }
    argc -= optind;
    argv += optind;

    /*
     * defaults
     */
    if (app == NULL) {
        app = strdup(BAR_DEFAULT_APP);
    }

    if (configfile == NULL) {
        configfile = strdup(BAR_DEFAULT_CONFIG_FILE);
    }

    if (host == NULL) {
        host = strdup(BAR_DEFAULT_HOST);
    }

    if (port == NULL) {
        port = strdup(BAR_DEFAULT_PORT);
    }

    /*
     * validate config
     */
    if (testconfig() != 0) {
        err(1, "File configuration error");
    }

    if (check_config) {
        TRACEC("Configuration check passed.\n");
        goto end;
    }

    /*
     * "real" configure
     */
    initall();

    if (configure(configfile) != 0) {
        FAIL("configure");
    }

    if (develop) {
        CTRACE("will run in develop mode");
    } else {
        /*
         * daemonize
         */
        //daemon_ize();
    }

    (void)mnthr_init();
    (void)MNTHR_SPAWN("run0", run0, argc, argv);
    (void)mnthr_loop();
    (void)mnthr_fini();

end:

    mnfcgi_app_destroy(&fcgi_app);
    free(app);
    free(configfile);
    free(host);
    free(port);
    return 0;
}

