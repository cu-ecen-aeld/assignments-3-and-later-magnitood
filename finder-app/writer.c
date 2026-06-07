#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int priority = LOG_USER | LOG_ERR;
    if (argc < 3) {
        syslog(priority, "Invalid Arguments\n");
        exit(1);
    }

    char *path     = argv[1];
    char *writestr = argv[2];
    size_t writestr_len = strlen(writestr);

    FILE *file = fopen(path, "w+");
    if (file == NULL) {
        syslog(priority, "Failed to open file: %s\n", strerror(errno));
        exit(1);
    }

    syslog(LOG_USER | LOG_DEBUG, "Writing %s to %s\n", writestr, path);
    int ret = fwrite(writestr, writestr_len, 1, file);
    if (ret == 0) {
        syslog(priority, "Failed to write file: %s\n", strerror(errno));
        exit(1);
    }

    return 0;
}
