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

#define MAX_RECV 65535   /* biggest UDP payload we accept */

/* ----------------------------------------------------------------- */
static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s <hostname> <port> <payload> <retries> <interval_ms>\n"
        "\n"
        "  hostname      DNS name or IP address of the remote host\n"
        "  port          UDP port number (or service name)\n"
        "  payload       Text that will be sent in the UDP datagram\n"
        "  retries       Number of send attempts (>=1)\n"
        "  interval_ms   Milliseconds to wait between retries\n",
        progname);
    exit(1);
}

/* ----------------------------------------------------------------- */
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

/* ----------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    if (argc != 6)
        usage(argv[0]);

    const char *host   = argv[1];
    const char *port   = argv[2];
    const char *payload = argv[3];
    long retries       = parse_long(argv[4], "retries");
    long interval_ms   = parse_long(argv[5], "interval_ms");

    if (retries < 1) {
        fprintf(stderr, "retries must be >= 1\n");
        exit(1);
    }

    /* ------------------- resolve host+port ----------------------- */
    struct addrinfo hints, *addrlist, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* <<< force IPv4 (or AF_UNSPEC for fallback) */
    hints.ai_socktype = SOCK_DGRAM;   /* UDP */

    int rc = getaddrinfo(host, port, &hints, &addrlist);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo(%s,%s): %s\n",
                host, port, gai_strerror(rc));
        exit(2);
    }

    unsigned char recv_buf[MAX_RECV];
    int got_reply = 0;

    /* --------------------------------------------------------------- */
    /* Try each address returned by getaddrinfo() until we get a reply */
    /* --------------------------------------------------------------- */
    for (rp = addrlist; rp != NULL && !got_reply; rp = rp->ai_next) {
        /* ----- create socket for this address --------------------- */
        int sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            perror("socket");
            continue;               /* try next address */
        }

        /* ----- set receive timeout (same as the retry interval) ---- */
        struct timeval tv;
        tv.tv_sec  = interval_ms / 1000;
        tv.tv_usec = (interval_ms % 1000) * 1000;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
            perror("setsockopt");
            close(sock);
            continue;
        }

        /* ----- retry loop for this address ----------------------- */
        for (long attempt = 0; attempt < retries && !got_reply; ++attempt) {
            /* ---- send ------------------------------------------------ */
            ssize_t sent = sendto(sock,
                                  payload,
                                  strlen(payload),
                                  0,
                                  rp->ai_addr,
                                  rp->ai_addrlen);
            if (sent == -1) {
                perror("sendto");
                break;              /* go to next address */
            }

            /* ---- wait for a reply ----------------------------------- */
            fd_set readset;
            FD_ZERO(&readset);
            FD_SET(sock, &readset);

            int sel = select(sock + 1, &readset, NULL, NULL, &tv);
            if (sel == -1) {
                perror("select");
                break;              /* go to next address */
            } else if (sel == 0) {   /* timeout */
                if (attempt + 1 < retries) {
                    usleep(interval_ms * 1000);   /* pause before next try */
                }
                continue;           /* retry same address */
            }

            /* ---- receive -------------------------------------------- */
            struct sockaddr_storage src;
            socklen_t src_len = sizeof(src);
            ssize_t recv_len = recvfrom(sock,
                                        recv_buf,
                                        sizeof(recv_buf),
                                        0,
                                        (struct sockaddr *)&src,
                                        &src_len);
            if (recv_len == -1) {
                perror("recvfrom");
                break;              /* go to next address */
            }

            /* ---- output reply --------------------------------------- */
            if (fwrite(recv_buf, 1, (size_t)recv_len, stdout) !=
                (size_t)recv_len) {
                perror("fwrite");
                close(sock);
                freeaddrinfo(addrlist);
                exit(5);
            }
            fflush(stdout);
            got_reply = 1;          /* success – leave all loops */
        }

        close(sock);
    }

    freeaddrinfo(addrlist);

    if (got_reply)
        return 0;                   /* success */
    fprintf(stderr,
            "No reply received after %ld attempt(s)\n",
            retries);
    return 6;                       /* timeout */
}
