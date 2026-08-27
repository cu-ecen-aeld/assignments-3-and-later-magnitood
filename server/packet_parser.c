typedef enum {
    Packet_SEEK_CMD,
    Packet_WRITE_AND_REPEAT,
    Packet_WRITE,
    Packet_KIND_COUNT,
} Packet_Kind;

typedef struct {
    Packet_Kind kind;
    char *buf;

    // Packet_SEEK_CMD
    struct aesd_seekto seek_cmd;

    // Packet_WRITE_AND_REPEAT, Packet_WRITE
    size_t size;
    size_t newline_loc;
} Packet;

bool parse_number(char *buf, size_t bufsize, size_t *i, uint32_t *num)
{
    if (!isdigit(buf[*i])) return false;

    uint32_t n = 0;
    for (; *i < bufsize && isdigit(buf[*i]); *i += 1) n = n*10 + (buf[*i]-'0');

    *num = n;
    return true;
}

bool parse_seekto_cmd(char *buf, size_t bufsize, struct aesd_seekto *seek_cmd)
{
    char s[] = "AESDCHAR_IOCSEEKTO:";
    size_t min = (sizeof(s) < bufsize) ? sizeof(s)-1 : bufsize;
    size_t i = 0;

    for (; i < min; i++) if (s[i] != buf[i]) return false;

    if (!parse_number(buf, bufsize, &i, &seek_cmd->write_cmd)) return false;

    if (i < bufsize && buf[i] != ',') return false;
    i++;

    if (!parse_number(buf, bufsize, &i, &seek_cmd->write_cmd_offset)) return false;

    return true;
}

void parse_packet(Packet *p, char *buf, size_t bufsize)
{
    debugf("parsing msg: %.*s\n", (int)bufsize, buf);
    struct aesd_seekto seek_cmd;
    if (parse_seekto_cmd(buf, bufsize, &seek_cmd)) {
        p->kind     = Packet_SEEK_CMD;
        p->seek_cmd = seek_cmd;
    } else {
        bool newline_present = false;
        size_t i;
        for (i = 0; i < bufsize; i++) {
            if (buf[i] == '\n') {
                newline_present = true;
                break;
            }
        }

        p->size        = bufsize;
        p->buf         = buf;
        p->newline_loc = i;
        p->kind        = (newline_present) ? Packet_WRITE_AND_REPEAT : Packet_WRITE;
    }
}
