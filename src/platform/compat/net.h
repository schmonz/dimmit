#ifndef DIMMIT_PLATFORM_COMPAT_NET_H
#define DIMMIT_PLATFORM_COMPAT_NET_H

/* Cross-platform Unix-domain-socket glue. POSIX uses <sys/socket.h> + int fds;
 * Windows (10 1803+) supports AF_UNIX through Winsock with <afunix.h>, but the
 * socket is a SOCKET (unsigned) handle, needs WSAStartup, and closes with
 * closesocket(). recv()/send() are spelled directly at call sites -- they exist
 * on both platforms -- so this header only abstracts what actually differs. */

#ifdef _WIN32
  #include <winsock2.h>
  #include <afunix.h>
  typedef SOCKET dimmit_sock_t;
  #define DIMMIT_BAD_SOCK INVALID_SOCKET
  static inline int  net_startup(void) {
      WSADATA wsa;
      return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
  }
  static inline void net_cleanup(void) { WSACleanup(); }
  static inline int  net_close(dimmit_sock_t s) { return closesocket(s); }
#else
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <unistd.h>
  typedef int dimmit_sock_t;
  #define DIMMIT_BAD_SOCK (-1)
  static inline int  net_startup(void) { return 0; }
  static inline void net_cleanup(void) { }
  static inline int  net_close(dimmit_sock_t s) { return close(s); }
#endif

#endif /* DIMMIT_PLATFORM_COMPAT_NET_H */
