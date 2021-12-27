#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "request.h"
#include "io_helper.h"
#include "include/common_threads.h"

char default_root[] = ".";
char *default_policy = "FIFO";

pthread_cond_t empty, fill;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int buf_sz = 1;
request_t **buffer = NULL; // to be allocated on the fly

int worker_cnt = 1;

int fill_ptr = 0;
int use_ptr = 0;

/* Size of connected fd buffer */
volatile int count = 0;

/* The Scheduling Policies : First-in-First-out (FIFO) or Smallest File First (SFF) */
char *policy;

void add_request_fifo(request_t *req);

void add_request_sff(request_t *req);

void create_workers();

const char *supported_policies[] = {"FIFO", "SFF"};

int policy_len() {
    return sizeof(supported_policies) / sizeof(char *);
}

void (*queue_request_funcs[])(request_t *r) = {
        add_request_fifo,
        add_request_sff
};

void print_buf() {
//    printf("=================================================\n");
//    request_t **pt = buffer;
//    for (int i = 0; i < count; ++i) {
//        printf(" | %d, file:%s, count:%d", (*(pt + i))->fd, (*(pt + i))->filename, count);
//    }
//    printf("\n>>> filled pos : %d | use pos :  %d\n", fill_ptr, use_ptr);
//    printf("=================================================\n\n");
}

void add_request_fifo(request_t *req) {
    buffer[fill_ptr] = req;
    fill_ptr = (fill_ptr + 1) % buf_sz;
    count++;
}

void add_request_sff(request_t *req) {
    if (count == 0 || req->cgiargs != NULL) {
        add_request_fifo(req);
        return;
    }

    int index = -1;
    int pre_size = 0;
    int filesize = req->filesize;
    for (int j = 0; j < count; ++j) {
        if ((*buffer + (use_ptr + j) % buf_sz)->filesize > filesize) {
            index = j;
            break;
        } else {
            ++pre_size;
        }
    }
    if (index < 0) {
        int tear = (use_ptr + count) % buf_sz;
        buffer[tear] = req;
    } else {
        // move forward the elements with larger file size than current req in the circular buffer
        int moved_size = count - pre_size;
        int target_pos = use_ptr + index;
        for (int i = moved_size - 1; i >= 0; i--) {
            buffer[(target_pos + i + 1) % buf_sz] = buffer[(target_pos + i) % buf_sz];
        }

        buffer[target_pos % buf_sz] = req;
    }

    fill_ptr = (use_ptr + count + 1) % buf_sz;
    count++;
}

request_t *get_connected_fd() {
    request_t *tmp = buffer[use_ptr];
    use_ptr = (use_ptr + 1) % buf_sz;
    count--;
    return tmp;
}

void add_task(request_t *fd, void (*queue_request)(request_t *r)) {
    Pthread_mutex_lock(&mutex);
    while (count == buf_sz)
        Pthread_cond_wait(&empty, &mutex)

    queue_request(fd);

    print_buf();
    Pthread_cond_signal(&fill);
    Pthread_mutex_unlock(&mutex);
}

void *handle_request(void *args) {
    int index = *((int *) args);
    printf(">>> thread index : %d\n", index);

//    sleep(10); // waiting for queue adjustment, for testing purpose.

    while (1) {
        Pthread_mutex_lock(&mutex);
        while (0 == count)
            Pthread_cond_wait(&fill, &mutex);
        request_t *fd = get_connected_fd();
        print_buf();
        Pthread_cond_signal(&empty);
        Pthread_mutex_unlock(&mutex);

        printf(">>> thread index : %d is working now with fd : %d \n", index, fd->fd);

        if (fd->static_type) {
            request_serve_static(fd->fd, fd->filename, fd->filesize);
            free_res(fd);
        } else {
            request_t **ppt = (request_t **) malloc(sizeof(request_t *));
            if (ppt == NULL) {
                perror("err: malloc()");
                exit(1);
            }

            *ppt = generate_request(fd->fd, fd->filename, 0, fd->cgiargs, fd->filesize);
            // keep the connected fd to the very end
            free_res_ext(fd, 1);

            request_serve_dynamic(ppt, free_res_dyna);
        }

        printf(">>> thread index : %d is idle\n", index);
    }

    return NULL;
}

void callback(request_t *pt_req) {
    for (int i = 0; i < policy_len(); ++i) {
        if (!strcmp(supported_policies[i], policy)) {
            add_task(pt_req, queue_request_funcs[i]);
            break;
        }
    }
}

void preprocess_request(int fd) {
    request_handle(fd, callback);
}

//
// ./wserver [-d <basedir>] [-p <portnum>] [-t threads] [-b buffers] [-s schedalg]
// 
int main(int argc, char *argv[]) {
    int c;
    char *root_dir = default_root;
    int port = 10000;

    policy = default_policy;

    while ((c = getopt(argc, argv, "d:p:t:b:s:")) != -1)
        switch (c) {
            case 'd':
                root_dir = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 't':
                worker_cnt = atoi(optarg);
                break;
            case 'b':
                buf_sz = atoi(optarg);
                buffer = (request_t **) malloc(sizeof(request_t *) * buf_sz);
                break;
            case 's':
                policy = optarg;
                if (strcmp("FIFO", policy) && strcmp("SFF", policy)) {
                    fprintf(stderr, "unsupported scheduling policy\n");
                    exit(1);
                }
                break;
            default:
                fprintf(stderr,
                        "usage: wserver [-d <basedir>] [-p <portnum>] [-t threads] [-b buffers] [-s schedalg]\n");
                exit(1);
        }

    printf(">>> scheduling policy : %s\n", policy);

    // run out of this directory
    chdir_or_die(root_dir);

    // now, get to work
    int listen_fd = open_listen_fd_or_die(port);

    create_workers();

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd, (sockaddr_t *) &client_addr, (socklen_t * ) & client_len);

        preprocess_request(conn_fd);
    }
    return 0;
}

void create_workers() {
    if (!buffer) {
        buffer = (request_t **) malloc(sizeof(request_t *) * buf_sz);
    }
    pthread_cond_init(&empty, NULL);
    pthread_cond_init(&fill, NULL);

    pthread_t pthread;
    for (int i = 0; i < worker_cnt; ++i) {
        int *pval = malloc(sizeof(int));
        *pval = i;

        Pthread_create(&pthread, NULL, handle_request, pval);
    }
}

