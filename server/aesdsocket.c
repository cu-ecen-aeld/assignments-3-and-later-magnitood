#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>

bool signal_recieved = false;

#define UNUSED(x) (void)x

void handler(int signum)
{
    UNUSED(signum);
    syslog(LOG_USER | LOG_INFO , "Caught Signal, exiting");
    signal_recieved = true;
}

void send_file_to_client(int sockfd, FILE *in_file)
{
    int infd = fileno(in_file);
    off_t read_offset = 0;

    // find the size of the file
    long store = ftell(in_file);
    fseek(in_file, 0, SEEK_END);
    long filesize = ftell(in_file);
    fseek(in_file, store, SEEK_SET);

    ssize_t bytes_send = sendfile(sockfd, infd, &read_offset, filesize);
    if (bytes_send == -1) { perror("sendfile"); exit(EXIT_FAILURE); };
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
    size_t bufsize = 4 * 4096; // 4 pages
    char *buf = malloc(sizeof(*buf) * bufsize);

    const char *path = "/var/tmp/aesdsocketdata";
    FILE *writefile = fopen(path, "w+");
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

    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_size = 0;

    while (!signal_recieved) {
        int cfd = accept(fd, (struct sockaddr *) &client_addr, &client_addr_size);
        if (cfd == -1 && errno == EINTR) break;

        char addr_ascii[32];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_ascii, sizeof(addr_ascii));
        syslog(LOG_USER | LOG_INFO , "Accepted connection from %s\n", addr_ascii);

        for (;;) {
            ssize_t bytes_recvd = recv(cfd, buf, bufsize, 0);
            if (bytes_recvd == -1) { perror("recv"); return 1; }
            if (bytes_recvd == 0) break;

            bool newline_present = false;
            ssize_t i;
            for (i = 0; i < bytes_recvd; i++) {
                if (buf[i] == '\n') {
                    newline_present = true;
                    break;
                }
            }

            if (newline_present) {
                fwrite(buf, 1, i+1, writefile);
                send_file_to_client(cfd, writefile);
                fwrite(buf+i+1, 1, bytes_recvd - (i+1), writefile);
            } else {
                fwrite(buf, 1, bytes_recvd, writefile);
            }
        }

        syslog(LOG_USER | LOG_INFO , "Closed connection from %s\n", addr_ascii);
        close(cfd);
    }

    fclose(writefile);
    close(fd);
    remove(path);
    free(buf);

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
    if (argc == 2) {//&& strncmp(argv[1], "-d", 2)) {
        printf("Daemonizing\n");
        ret = daemonize();
    } else {
        printf("Running\n");
        ret = run();
    }

    return ret;
}
