#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sys/syslog.h>
#include <unistd.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

#define DEFAULT_BUF_SIZE 4 * 4096
#define ARR_SIZE(arr) (sizeof(arr)/sizeof(*arr))

#define USE_AESD_CHAR_DEVICE

bool signal_recieved = false;

typedef struct Thread_Info {
    FILE *writefile;
    char *writefile_path;
    pthread_mutex_t *mutex; // mutex to lock the writing file
    char *addr;
    int cfd;
} Thread_Info;

typedef struct Thread_Ids {
    pthread_t *items;
    size_t count;
    size_t capacity;
} Thread_Ids;

void write_and_flush_file(const void *restrict buf, size_t size, size_t n, FILE *restrict file)
{
    fwrite(buf, size, n, file);
    fflush(file);
    // int fd = fileno(file);
    // write(fd, buf, n);
}

bool send_file_to_client(int sockfd, const char *filepath)
{
    FILE *in_file = fopen(filepath, "r");
    if (in_file == NULL) {
        syslog(LOG_USER | LOG_ERR, "fopen failed: %s\n", strerror(errno));
        return false;
    }

#ifdef USE_AESD_CHAR_DEVICE
    unsigned char buf[4096]; // TODO: make this buffer page aligned

    int read_offset = 0;
    while (true) {
        ssize_t bytes_read = pread(fileno(in_file), buf, sizeof(buf), read_offset);
        if (bytes_read == -1) {
            syslog(LOG_USER | LOG_ERR, "read failed: %s\n", strerror(errno));
            break;
        }
        if (bytes_read == 0) break;

        read_offset += bytes_read;

        ssize_t bytes_sent = send(sockfd, buf, bytes_read, 0);
        if (bytes_sent == -1) {
            syslog(LOG_USER | LOG_ERR, "send failed: %s\n", strerror(errno));
            break;
        }
    }
#else
    int infd = fileno(in_file);
    off_t read_offset = 0;

    struct stat st = {0};
    if (fstat(infd, &st) != 0) {
        syslog(LOG_USER | LOG_ERR, "fstat failed: %s\n", strerror(errno));
        return false;
    }

    syslog(LOG_USER | LOG_DEBUG, "Sending file of size: %zu\n", st.st_size);
    if (sendfile(sockfd, infd, &read_offset, st.st_size) == -1) {
        syslog(LOG_USER | LOG_ERR, "sendfile failed: %s\n", strerror(errno));
        return false;
    }
#endif // USE_AESD_CHAR_DEVICE

    fclose(in_file);
    return true;
}

void *handle_connection(void *ptr)
{
    void *ret = NULL;
    Thread_Info *p = (Thread_Info *) ptr;
    int cfd = p->cfd;
    // p->writefile;

    size_t bufsize = DEFAULT_BUF_SIZE;
    unsigned char *buf = malloc(sizeof(*buf) * bufsize);
    if (buf == NULL) goto cleanup_malloc;

    for (;;) {
        ssize_t bytes_recvd = recv(cfd, buf, bufsize, 0);
        if (bytes_recvd == -1) { perror("recv"); goto cleanup_malloc; }
        if (bytes_recvd == 0) break;

        bool newline_present = false;
        ssize_t i;
        for (i = 0; i < bytes_recvd; i++) {
            if (buf[i] == '\n') {
                newline_present = true;
                break;
            }
        }

        pthread_mutex_lock(p->mutex);
        if (newline_present) {
            write_and_flush_file(buf, 1, i+1, p->writefile);
            if (!send_file_to_client(cfd, p->writefile_path)) {
                pthread_mutex_unlock(p->mutex);
                goto cleanup_malloc;
            };
            write_and_flush_file(buf+i+1, 1, bytes_recvd - (i+1), p->writefile);
        } else {
            write_and_flush_file(buf, 1, bytes_recvd, p->writefile);
        }
        pthread_mutex_unlock(p->mutex);
    }
    ret = ptr;

cleanup_malloc:
    if (buf != NULL) free(buf);
    close(cfd);
    syslog(LOG_USER | LOG_INFO , "Closed connection from %s\n", p->addr);
    free(p->addr);
    free(p->writefile_path);
    free(p);
    return ret;
}

void *append_timer(void *ptr)
{
    Thread_Info *p = (Thread_Info *) ptr;
    struct timespec ts;
    struct tm tm;
    char time_string[128];

    while (!signal_recieved) {
        pthread_mutex_lock(p->mutex);
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            syslog(LOG_USER | LOG_ERR, "clock_gettime failed: %s\n", strerror(errno));
            pthread_mutex_unlock(p->mutex);
            return NULL;
        }

        if (localtime_r(&ts.tv_sec, &tm) == NULL) {
            syslog(LOG_USER | LOG_ERR, "trying to get localtime failed: %s\n", strerror(errno));
            pthread_mutex_unlock(p->mutex);
            return NULL;
        }

        size_t bytes_written = strftime(time_string, ARR_SIZE(time_string), "timestamp:%A %d %B %Y (%C-%m-%d) %T %Z\n", &tm);
        write_and_flush_file(time_string, 1, bytes_written, p->writefile);
        pthread_mutex_unlock(p->mutex);
        sleep(10);
    }

    return ptr;
}

void handler(int signum)
{
    UNUSED(signum);
    syslog(LOG_USER | LOG_INFO , "Caught Signal, exiting");
    signal_recieved = true;
}

bool start_signal_blocking_thread(pthread_t *tid, void *(*func)(void*), void *params) {
    sigset_t sigmask = {0}, old_mask = {0};
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);

    if (pthread_sigmask(SIG_SETMASK, &sigmask, &old_mask) != 0) {
        syslog(LOG_USER | LOG_ERR, "Failed to set sigmask: %s\n", strerror(errno));
        return false;
    }

    if (pthread_create(tid, NULL, func, params) != 0) {
        syslog(LOG_USER | LOG_ERR, "Failed to create thread: %s\n", strerror(errno));
        return false;
    }

    if (pthread_sigmask(SIG_SETMASK, &old_mask, NULL) != 0) {
        syslog(LOG_USER | LOG_ERR, "Failed to set sigmask: %s\n", strerror(errno));
        return false;
    }

    return true;
}

int run()
{
    // signal(SIGINT,  handler);
    // signal(SIGTERM, handler);

    struct sigaction sa = {0};
    sa.sa_handler = handler;

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // scratch buffer
    size_t bufsize = DEFAULT_BUF_SIZE; // 4 pages
    char *buf = malloc(sizeof(*buf) * bufsize);

#ifdef USE_AESD_CHAR_DEVICE
    const char *path = "/dev/aesdchar";
#else
    const char *path = "/var/tmp/aesdsocketdata";
#endif // USE_AESD_CHAR_DEVICE

    FILE *writefile = fopen(path, "r+");
    if (writefile == NULL) { perror("fopen"); return 1; }

    // since the lifetime of all the objects in this program is the entire program,
    // I am not doing cleanup like close() to simplify error handling code. The OS will cleanup for me
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) { perror("socket"); return 1; }

    struct sockaddr_in server_addr = {
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(9000),
        .sin_family      = AF_INET,
    };

    int ret = bind(fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (ret == -1) { perror("bind"); return 1; }

    ret = listen(fd, SOMAXCONN);
    if (ret == -1) { perror("listen"); return 1; }

    Thread_Ids thread_ids = {0};
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init(&mutex, NULL);

#ifndef USE_AESD_CHAR_DEVICE
    pthread_t timer_tid;
    Thread_Info timer_info = {
        .writefile = writefile,
        .mutex = &mutex,
    };
    if (!start_signal_blocking_thread(&timer_tid, append_timer, &timer_info)) {
        syslog(LOG_USER | LOG_ERR, "Starting timer thread failed\n");
        return 1;
    }
#endif // USE_AESD_CHAR_DEVICE

    while (!signal_recieved) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_addr_size = 0;

        int cfd = accept(fd, (struct sockaddr *) &client_addr, &client_addr_size);
        if (cfd == -1 && errno == EINTR) break;

        char addr_ascii[32];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_ascii, sizeof(addr_ascii));
        syslog(LOG_USER | LOG_INFO , "Accepted connection from %s\n", addr_ascii);

        pthread_t tid;
        Thread_Info *info = malloc(sizeof(*info));
        info->cfd = cfd;
        info->writefile = writefile;
        info->mutex = &mutex;
        info->addr = strdup(addr_ascii);
        if (info->addr == NULL) {
            syslog(LOG_USER | LOG_ERR, "strdup failed: %s\n", strerror(errno));
            break;
        }

        info->writefile_path = strdup(path);
        if (info->writefile_path == NULL) {
            syslog(LOG_USER | LOG_ERR, "strdup failed: %s\n", strerror(errno));
            break;
        }

        if (!start_signal_blocking_thread(&tid, handle_connection, info)) {
            syslog(LOG_USER | LOG_ERR, "Starting thread failed\n");
            break;
        }

        da_append(&thread_ids, tid);
    }

    for (size_t i = 0; i < thread_ids.count; i++)
        pthread_join(thread_ids.items[i], NULL);

    close(fd);
    fclose(writefile);
#ifndef USE_AESD_CHAR_DEVICE
    remove(path);
#endif // USE_AESD_CHAR_DEVICE
    free(buf);
    da_free(thread_ids);

    return 0;

}

int daemonize()
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        pid_t ret = fork();
        if (ret == 0) {
            int fd = open("/dev/null", O_RDWR);
            assert(fd != -1);
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            umask(0);
            chdir("/");
            close(fd);

            run();
        } else if (ret > 0) {
            exit(EXIT_SUCCESS);
            return 0;
        } else {
            exit(EXIT_FAILURE);
            return 1;
        }
    } else if (pid > 0) {
        return 0;
    } else {
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    if (argc == 2 && strncmp(argv[1], "-d", 2) == 0) {//&& strncmp(argv[1], "-d", 2)) {
        printf("Daemonizing\n");
        ret = daemonize();
    } else {
        printf("Running\n");
        ret = run();
    }

    return ret;
}
