#ifndef __REQUEST_H__

#ifndef MAXBUF
#define MAXBUF (8192)
#endif

#include "io_helper.h"

typedef struct request {
    int fd;
    char *filename;
    int static_type;
    int filesize;
    char *cgiargs;
} request_t;


request_t *generate_request(int fd, char *filename, int is_static, char *args, int size);

void free_res(request_t *req);

void free_res_dyna(request_t **req);

void free_res_ext(request_t *req, int fd_kept);

void request_handle(int fd, void (*func)(request_t *request_pt));

void request_serve_static(int fd, char *filename, int filesize);

void request_serve_dynamic(request_t **request, void (*cleanup)(request_t **req));

void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

#endif // __REQUEST_H__
