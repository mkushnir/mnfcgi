#ifndef MNFCGI_TESTMY_H_DEFINED
#define MNFCGI_TESTMY_H_DEFINED

#include <mnfcgi.h>

#ifdef __cplusplus
extern "C" {
#endif


ssize_t testmy_params(mnfcgi_record_t *, mnbytestream_t *, void *);
ssize_t testmy_stdin(mnfcgi_record_t *, mnbytestream_t *, void *);
ssize_t testmy_data(mnfcgi_record_t *, mnbytestream_t *, void *);

#ifdef __cplusplus
}
#endif
#endif /* MNFCGI_TESTMY_H_DEFINED */

