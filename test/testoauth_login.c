#include <mrkcommon/dumpm.h>
#include <mrkcommon/util.h>

#include <mrkthr.h>

#include <mnfcgi_app_private.h>

#include "testoauth.h"
#include "diag.h"

static mnbytes_t _found = BYTES_INITIALIZER("Found...");

#if 0
static mnbytes_t _ok = BYTES_INITIALIZER("MRK");
static mnbytes_t _internal_server_error =
    BYTES_INITIALIZER("Internal Server Error");
#endif

static mnbytes_t _param_http_authorization =
    BYTES_INITIALIZER("HTTP_AUTHORIZATION");
static mnbytes_t _unauthorized = BYTES_INITIALIZER("Unauthorized");
static mnbytes_t _www_authenticate =
    BYTES_INITIALIZER("WWW-Authenticate");
static mnbytes_t _forbidden = BYTES_INITIALIZER("Forbidden");
static mnbytes_t _bad_request = BYTES_INITIALIZER("Bad Request");

static mnbytes_t _response_type = BYTES_INITIALIZER("response_type");
static mnbytes_t _code = BYTES_INITIALIZER("code");
static mnbytes_t _client_id = BYTES_INITIALIZER("client_id");
static mnbytes_t _redirect_uri = BYTES_INITIALIZER("redirect_uri");
static mnbytes_t _state = BYTES_INITIALIZER("state");


static mnbytes_t _authorization_service = BYTES_INITIALIZER("Authorization Service");


#define CHECK_AUTHORIZATION_REDIRECT_URI_OK     0x01
#define CHECK_AUTHORIZATION_REDIRECT_CREDS_OK   0x02
static int
check_authorization(mnbytes_t *response_type,
                    mnbytes_t *client_id,
                    mnbytes_t *redirect_uri,
                    mnbytes_t *authorization,
                    UNUSED mnbytes_t *state,
                    mnbytes_t **code) {
    int res = 0;
    mnbytes_t *a = NULL;

    assert(response_type != NULL);
    assert(client_id != NULL);
    assert(redirect_uri != NULL);
    assert(authorization != NULL);

    /*
     * only the "code" authorization flow is supported
     */
    if (bytes_cmp(response_type, &_code) != 0) {
        goto end;
    }

    if (testoauth_verify_client_nonsecure(BCDATA(client_id),
                                          BCDATA(redirect_uri)) != 0) {
        goto end;
    }
    res |= CHECK_AUTHORIZATION_REDIRECT_URI_OK;

    if (bytes_startswith(authorization, BYTES_REF("Basic "))) {
        a = bytes_new(BSZ(authorization) - 6);
        memcpy(BDATA(a), BDATA(authorization) + 6, BSZ(a));

        if (bytes_base64_decode_url(a) == 0) {
            char *colon;

            if ((colon = strchr(BCDATA(a), ':')) != NULL) {
                *colon = '\0';

                *code = NULL;
                if (testoauth_verify_cred(
                        BCDATA(a), colon + 1, code) == 0) {
                    res |= CHECK_AUTHORIZATION_REDIRECT_CREDS_OK;
                }

            } else {
            }
        }
    } else {
    }

end:
    BYTES_DECREF(&a);
    return res;
}

int
testoauth_login_get(mnfcgi_request_t *req, UNUSED void *udata)
{
    mnbytes_t *authorization;

    if ((authorization = mnfcgi_request_get_param(
                req, &_param_http_authorization)) == NULL) {

        goto err401;

    } else {
        int res;
        mnhash_item_t *hit;
        mnbytes_t *response_type;
        mnbytes_t *client_id;
        mnbytes_t *redirect_uri;
        mnbytes_t *state;
        mnbytes_t *code;
        mnbytes_t *uri;

        if ((hit = hash_get_item(
                        &req->info.query_terms, &_response_type)) == NULL) {
            goto err403;
        }
        response_type = hit->value;

        if ((hit = hash_get_item(
                        &req->info.query_terms, &_client_id)) == NULL) {
            goto err403;
        }
        client_id = hit->value;

        if ((hit = hash_get_item(
                        &req->info.query_terms, &_redirect_uri)) == NULL) {
            goto err403;
        }
        redirect_uri = hit->value;

        if ((hit = hash_get_item(&req->info.query_terms, &_state)) != NULL) {
            state = hit->value;
        } else {
            state = NULL;
        }

        //CTRACE("authorization: %s", BDATA(authorization));
        //CTRACE("response_type=%s", BDATA(response_type));
        //CTRACE("client_id=%s", BDATA(client_id));
        //CTRACE("redirect_uri=%s", BDATA(redirect_uri));
        //CTRACE("state=%s", BDATASAFE(state));
        code = NULL;
        res = check_authorization(response_type,
                                  client_id,
                                  redirect_uri,
                                  authorization,
                                  state,
                                  &code);
        if (!(res & CHECK_AUTHORIZATION_REDIRECT_CREDS_OK)) {
            if (res & CHECK_AUTHORIZATION_REDIRECT_URI_OK) {
                if (state == NULL) {
                    uri = bytes_printf("%s?error=invalid_creds",
                                       BDATA(redirect_uri));
                } else {
                    uri = bytes_printf("%s?error=invalid_creds&state=%s",
                                       BDATA(redirect_uri),
                                       BDATA(state));
                }

            } else {
                BYTES_DECREF(&code);
                goto err400;
            }

        } else {
            if (state == NULL) {
                uri = bytes_printf("%s?code=%s",
                                   BDATA(redirect_uri),
                                   BDATA(code));
            } else {
                uri = bytes_printf("%s?code=%s&state=%s",
                                   BDATA(redirect_uri),
                                   BDATA(code),
                                   BDATA(state));
            }
        }

        BYTES_DECREF(&code);

        BYTES_INCREF(uri);
        mnfcgi_app_redir(req, 302, &_found, uri);
        BYTES_DECREF(&uri);
    }

end:
    return 0;

err400:
    mnfcgi_app_error(req, 400, &_bad_request);
    goto end;

err401:
    (void)mnfcgi_request_field_addf(req, 0,
                                    &_www_authenticate,
                                    "Basic realm=\"%s\"",
                                    BDATA(&_authorization_service));

    mnfcgi_app_error(req, 401, &_unauthorized);
    goto end;

err403:
    mnfcgi_app_error(req, 403, &_forbidden);
    goto end;

//err500:
//    mnfcgi_app_error(req, 500, &_internal_server_error);
//    goto end;

}


