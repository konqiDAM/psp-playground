/*
 * net_sock.h - small PSP socket helpers with the firmware's 8-byte timeval.
 */
#ifndef PSP_NET_SOCK_H
#define PSP_NET_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PSP_SOCK_OK = 0,
    PSP_SOCK_OK_REFUSED = 1,
    PSP_SOCK_ERR_TIMEOUT = -1,
    PSP_SOCK_ERR_UNREACHABLE = -2,
    PSP_SOCK_ERR_BAD_ADDR = -3,
    PSP_SOCK_ERR_SOCKET = -4,
    PSP_SOCK_ERR_IO = -5
} PspSockResult;

typedef struct {
    PspSockResult result;
    int elapsed_ms;
    int sys_errno;
} PspSockProbe;

int  psp_sock_set_nonblock(int fd, int on);
/* Returns an established fd, or -1. out receives OK/OK_REFUSED/error,
 * elapsed_ms and sys_errno; OK_REFUSED means the peer sent a TCP RST. */
int  psp_sock_connect_ex(const char *ip, unsigned short port, int timeout_ms,
                         PspSockProbe *out);
/* fd-only wrapper: refused and every other non-OK result become -1. */
int  psp_sock_connect_timeout(const char *ip, unsigned short port,
                              int timeout_ms, int *elapsed_ms, int *sys_errno);
int  psp_sock_tcp_probe(const char *ip, unsigned short port, int timeout_ms,
                        PspSockProbe *out);
int  psp_sock_send_all(int fd, const void *buf, int len, int timeout_ms);
int  psp_sock_recv_nb(int fd, void *buf, int len);   /* -2 = would block */
int  psp_sock_wait_readable(int fd, int timeout_ms); /* 1/0/-1 */
void psp_sock_close_abortive(int fd);

#ifdef __cplusplus
}
#endif

#endif /* PSP_NET_SOCK_H */
