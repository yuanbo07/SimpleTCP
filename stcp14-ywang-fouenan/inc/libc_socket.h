/*
 * libc_socket.h
 */

#ifndef _LIBC_SOCKET_H_
#define _LIBC_SOCKET_H_


#include <sys/socket.h>
#define PKT_ERROR_RATE 0
#define RAND_MAX 10


/*Function to be used to generate random loss 
 *return 1 with probability p and 0 with a probability 1-p
 */
int flip_coin(double p);


/* Functions that wraps the libc. Basically initialize a function pointer the
 * first time a function is called, and then directly call the libc socket api
 */
int libc_socket(int domain, int type, int protocol);
int libc_bind (int fd, const struct sockaddr *addr, socklen_t len);
int libc_connect (int fd, const struct sockaddr *addr, socklen_t len);
ssize_t libc_send (int fd, const void *buf, size_t n, int flags);
ssize_t libc_recv (int fd, void *buf, size_t n, int flags);
ssize_t libc_sendto(int fd, const void *buf, size_t n, int flags, 
                    const struct sockaddr *addr, socklen_t addr_len);
ssize_t libc_recvfrom(int fd, void *buf, size_t n, int flags, 
                      struct sockaddr *addr, socklen_t *addr_len);
ssize_t libc_sendmsg (int fd, const struct msghdr *message, int flags);
ssize_t libc_recvmsg (int fd, struct msghdr *message, int flags);
int libc_listen (int fd, int n);
int libc_accept (int fd, struct sockaddr *addr, socklen_t *addr_len);
int libc_shutdown (int fd, int how);
int libc_close (int fd);
ssize_t libc_read (int fd, void *buf, size_t n);
ssize_t libc_write (int fd, const void *buf, size_t n);
int libc_getsockname (int fd, struct sockaddr *addr, socklen_t *len);
int libc_getpeername (int fd, struct sockaddr *addr, socklen_t *len);
int libc_getsockopt (int fd, int level, int optname, void *optval, 
        		     socklen_t *optlen);
int libc_setsockopt (int fd, int level, int optname, const void *optval,
                     socklen_t optlen);

#endif /* _LIBC_SOCKET_H_ */

/* vim: set expandtab ts=4 sw=4 tw=80: */
