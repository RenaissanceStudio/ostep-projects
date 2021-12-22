#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "request.h"
#include "io_helper.h"
#include "include/common_threads.h"

char default_root[] = ".";

pthread_cond_t empty, fill;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int buf_sz = 1;
int *buffer; // to be allocated on the fly

int worker_cnt = 1;

int fill_ptr = 0;
int use_ptr = 0;

/* Size of connected fd buffer */
int count = 0;

void create_workers();

void print_buf() {
    int *pt = buffer;
    for (int i = 0; i < count; ++i) {
        printf(" | %d ", *(pt + i));
        ++pt;
    }
    printf("| filled pos : %d | use pos :  %d\n", fill_ptr, use_ptr);
}

void add_connected_fd(int fd) {
    buffer[fill_ptr] = fd;
    fill_ptr = (fill_ptr + 1) % buf_sz;
    count++;
}

int get_connected_fd() {
    int tmp = buffer[use_ptr];
    use_ptr = (use_ptr + 1) % buf_sz;
    count--;
    return tmp;
}

void add_task(int fd) {
    Pthread_mutex_lock(&mutex);
    while (count == buf_sz)
        Pthread_cond_wait(&empty, &mutex)
    add_connected_fd(fd);
    Pthread_cond_signal(&fill);
    Pthread_mutex_unlock(&mutex);
}

void *handle_request(void *args) {
    int index = *((int *) args);
    printf(">>> thread index : %d\n", index);
    while (1) {
        Pthread_mutex_lock(&mutex);
        while (0 == count)
            Pthread_cond_wait(&fill, &mutex);
        int fd = get_connected_fd();
        Pthread_cond_signal(&empty);
        Pthread_mutex_unlock(&mutex);

        printf(">>> thread index : %d is working now with fd : %d \n", index, fd);
        request_handle(fd);
        close_or_die(fd);
        printf(">>> thread index : %d is idle\n", index);
    }

    return NULL;
}

//
// ./wserver [-d <basedir>] [-p <portnum>] [-t threads] [-b buffers]
// 
int main(int argc, char *argv[]) {
    int c;
    char *root_dir = default_root;
    int port = 10000;

    while ((c = getopt(argc, argv, "d:p:t:b:")) != -1)
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
                buffer = (int *) malloc(sizeof(int) * buf_sz);
                break;
            default:
                fprintf(stderr, "usage: wserver [-d basedir] [-p port]\n");
                exit(1);
        }

    // run out of this directory
    chdir_or_die(root_dir);

    // now, get to work
    int listen_fd = open_listen_fd_or_die(port);

    create_workers();

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd, (sockaddr_t *) &client_addr, (socklen_t * ) & client_len);

        add_task(conn_fd);
    }
    return 0;
}

void create_workers() {
    if (!buffer) {
        buffer = (int *) malloc(sizeof(int));
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

