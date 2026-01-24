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

/* Simple SHA-256 implementation (public domain) */
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t data[])
{
    uint32_t a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];
    for (i=0,j=0; i<16; ++i, j+=4)
        m[i] = (data[j] << 24) | (data[j+1] << 16) | (data[j+2] << 8) | (data[j+3]);
    for ( ; i<64; ++i)
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];
    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];
    for (i=0; i<64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d + t1; d=c; c=b; b=a; a=t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    size_t i, rem = len % 64;
    for (i=0; i+64 <= len; i+=64)
        sha256_transform(state, data+i);
    uint8_t block[64];
    memcpy(block, data+i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        memset(block+rem+1, 0, 64-rem-1);
        sha256_transform(state, block);
        memset(block, 0, 56);
    } else {
        memset(block+rem+1, 0, 64-rem-1);
    }
    uint64_t bitlen = (uint64_t)len * 8;
    block[63] = bitlen & 0xff;
    block[62] = (bitlen >> 8) & 0xff;
    block[61] = (bitlen >> 16) & 0xff;
    block[60] = (bitlen >> 24) & 0xff;
    block[59] = (bitlen >> 32) & 0xff;
    block[58] = (bitlen >> 40) & 0xff;
    block[57] = (bitlen >> 48) & 0xff;
    block[56] = (bitlen >> 56) & 0xff;
    sha256_transform(state, block);
    for (i=0; i<8; ++i) {
        out[i*4]   = (state[i] >> 24) & 0xff;
        out[i*4+1] = (state[i] >> 16) & 0xff;
        out[i*4+2] = (state[i] >> 8) & 0xff;
        out[i*4+3] = state[i] & 0xff;
    }
}

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
    unsigned char *static_key_buf = NULL;
    size_t static_key_len = 0;

    if (hash_mode) {
        FILE *kf = fopen(static_key_path, "rb");
        if (!kf) {
            perror("static key file open");
            freeaddrinfo(addrlist);
            exit(3);
        }
        fseek(kf, 0, SEEK_END);
        long fsize = ftell(kf);
        if (fsize < 0) fsize = 0;
        if (fsize > 4096) fsize = 4096;
        fseek(kf, 0, SEEK_SET);
        static_key_buf = malloc((size_t)fsize);
        if (!static_key_buf) {
            perror("malloc");
            fclose(kf);
            freeaddrinfo(addrlist);
            exit(4);
        }
        size_t readb = fread(static_key_buf, 1, (size_t)fsize, kf);
        if (readb != (size_t)fsize) {
            perror("static key read");
            free(static_key_buf);
            fclose(kf);
            freeaddrinfo(addrlist);
            exit(5);
        }
        static_key_len = (size_t)fsize;
        fclose(kf);
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
                sha256(combined, total_len, hash);
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
    if (static_key_buf) free(static_key_buf);

    if (got_reply)
        return 0;
    fprintf(stderr, "No reply received after %ld attempt(s)\n", retries);
    return 6;
}
