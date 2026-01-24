/* -------------------------------------------------------------
   udp-client.c   – static UDP request / first‑reply client
   ------------------------------------------------------------- */
#define _POSIX_C_SOURCE 200112L   /* getaddrinfo(), usleep() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>          /* usleep() */
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <sys/select.h>
#include <stdint.h>
#include "sha-256.h"
#include <fcntl.h>

#define MAX_RECV 65535   /* biggest UDP payload we accept */

static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s <hostname> <port> <payload> <retries> <interval_ms> [--hash <static_key_file>]\n"
        "\n"
        "  hostname      DNS name or IP address of the remote host\n"
        "  port          UDP port number (or service name)\n"
        "  payload       Text that will be sent in the UDP datagram\n"
        "  retries       Number of send attempts (>=1)\n"
        "  interval_ms   Milliseconds to wait between retries\n"
        "  --hash <static_key_file>   Compute SHA‑256 of (static key + reply) and output hex digest\n",
        progname);
    exit(1);
}

static long parse_long(const char *s, const char *name)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < 0) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(1);
    }
    return v;
}

int main(int argc, char *argv[])
{
    int hash_mode = 0;
    const char *static_key_path = NULL;

    if (argc != 6 && argc != 8) {
        usage(argv[0]);
    }

    const char *host   = argv[1];
    const char *port   = argv[2];
    const char *payload = argv[3];
    long retries       = parse_long(argv[4], "retries");
    long interval_ms   = parse_long(argv[5], "interval_ms");

    if (argc == 8) {
        if (strcmp(argv[6], "--hash") != 0) {
            usage(argv[0]);
        }
        hash_mode = 1;
        static_key_path = argv[7];
    }

    if (retries < 1) {
        fprintf(stderr, "retries must be >= 1\n");
        exit(1);
    }

    struct addrinfo hints, *addrlist, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int rc = getaddrinfo(host, port, &hints, &addrlist);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s,%s): %s\n", host, port, gai_strerror(rc));
        exit(2);
    }

    unsigned char recv_buf[MAX_RECV];
    int got_reply = 0;
    unsigned char static_key_buf[4096];
    size_t static_key_len = 0;

    if (hash_mode) {
        int fd = open(static_key_path, O_RDONLY);
        if (fd == -1) {
            perror("static key file open");
            freeaddrinfo(addrlist);
            exit(3);
        }
        /* Read up to 4096 bytes using low‑level read (works for regular files and pipes) */
        size_t max_len = sizeof(static_key_buf);
        size_t total = 0;
        while (total < max_len) {
            ssize_t r = read(fd, static_key_buf + total, max_len - total);
            if (r == 0) {
                break; // EOF
            }
            if (r < 0) {
                perror("static key read");
                close(fd);
                freeaddrinfo(addrlist);
                exit(5);
            }
            total += (size_t)r;
        }
        static_key_len = total;
        /* Trim trailing newline or carriage return that may be present */
        while (static_key_len > 0 && (static_key_buf[static_key_len-1] == '\n' || static_key_buf[static_key_len-1] == '\r')) {
            static_key_len--;
        }
    }

    for (rp = addrlist; rp != NULL && !got_reply; rp = rp->ai_next) {
        int sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            perror("socket");
            continue;
        }
        struct timeval tv;
        tv.tv_sec  = interval_ms / 1000;
        tv.tv_usec = (interval_ms % 1000) * 1000;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
            perror("setsockopt");
            close(sock);
            continue;
        }
        for (long attempt = 0; attempt < retries && !got_reply; ++attempt) {
            ssize_t sent = sendto(sock, payload, strlen(payload), 0, rp->ai_addr, rp->ai_addrlen);
            if (sent == -1) {
                perror("sendto");
                break;
            }
            fd_set readset;
            FD_ZERO(&readset);
            FD_SET(sock, &readset);
            int sel = select(sock + 1, &readset, NULL, NULL, &tv);
            if (sel == -1) {
                perror("select");
                break;
            } else if (sel == 0) {
                if (attempt + 1 < retries) {
                    usleep(interval_ms * 1000);
                }
                continue;
            }
            struct sockaddr_storage src;
            socklen_t src_len = sizeof(src);
            ssize_t recv_len = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&src, &src_len);
            if (recv_len == -1) {
                perror("recvfrom");
                break;
            }
            if (hash_mode) {
                size_t total_len = static_key_len + (size_t)recv_len;
                unsigned char *combined = malloc(total_len);
                if (!combined) {
                    perror("malloc combined");
                    close(sock);
                    freeaddrinfo(addrlist);
                    exit(6);
                }
                memcpy(combined, static_key_buf, static_key_len);
                memcpy(combined + static_key_len, recv_buf, (size_t)recv_len);
                unsigned char hash[32];
                calc_sha_256(hash, combined, total_len);
                free(combined);
                if (fwrite(hash, 1, 32, stdout) != 32) {
                    perror("fwrite hash");
                }
                fflush(stdout);
            } else {
                if (fwrite(recv_buf, 1, (size_t)recv_len, stdout) != (size_t)recv_len) {
                    perror("fwrite");
                    close(sock);
                    freeaddrinfo(addrlist);
                    exit(5);
                }
                fflush(stdout);
            }
            got_reply = 1;
        }
        close(sock);
    }

    freeaddrinfo(addrlist);
    // static_key_buf is on stack; no need to free

    if (got_reply)
        return 0;
    fprintf(stderr, "No reply received after %ld attempt(s)\n", retries);
    return 6;
}
