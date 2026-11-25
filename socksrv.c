#include "socksrv.h"

#define BUFFER_SIZE 4096

static void
accept_close (int sockfd, int *nullfd)
{
  for (close (*nullfd);;)
    {
      int dropfd = accept4 (sockfd, null, null, SOCK_CLOEXEC);
      if (dropfd < 0)
        {
          if (errno == ECONNABORTED || errno == EPROTO || errno == EINTR)
            continue;
          break;
        }
      close (dropfd);
    }
  *nullfd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
}

static void
accept_spawn (int sockfd, int *nullfd, struct descriptor *q,
              void *(*spawn) (struct sockpeer))
{
  if (*nullfd < 0 && (*nullfd = open ("/dev/null", O_RDONLY | O_CLOEXEC)) < 0)
    return;
  for (;;)
    {
      struct sockaddr_storage addr;
      int connfd = accept4 (sockfd, (struct sockaddr *)&addr,
                            &(socklen_t){ sizeof (addr) },
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (connfd < 0)
        {
          if (errno == ECONNABORTED || errno == EPROTO || errno == EINTR)
            continue;
          if (errno == EMFILE || errno == ENFILE)
            accept_close (sockfd, nullfd);
          break;
        }
      struct corosuperblk *k = spawn ((struct sockpeer){ connfd, addr });
      if (k)
        xlist$ (push) (&k->lentry, &q->entry, &q->prop);
    }
}

static inline int
timercmp (struct timernod *lhs, struct timernod *rhs)
{
  u64 l = container_of (lhs, struct timersrq, timernod)->deadline;
  u64 r = container_of (rhs, struct timersrq, timernod)->deadline;
  return (l > r) - (l < r);
}

static struct timersrq *
timermin (struct timernod **root)
{
  struct timernod *mn = *root;
  if (!mn)
    return null;
  while (mn->left)
    mn = mn->left;
  *root = bst$ (splay) (mn);
  return container_of (mn, struct timersrq, timernod);
}

static struct descriptor
sched_loop (int sockfd, struct descriptor q, void *(*spawn) (struct sockpeer))
{
  autofd int epfd = epoll_create1 (EPOLL_CLOEXEC);
  if (epfd < 0)
    return (struct descriptor){ .prop = "epoll_create1 < 0", .ident = errno };
  struct epoll_event ev = { .events = EPOLLIN };
  if (epoll_ctl (epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
    return (struct descriptor){ .prop = "epoll_ctl < 0", .ident = errno };
  smartptr struct epoll_event *evs
      = malloc (sizeof (struct epoll_event[BUFFER_SIZE]));
  if (!evs)
    return (struct descriptor){ .prop = "malloc == null", .ident = errno };
  autofd int nullfd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
  if (nullfd < 0)
    return (struct descriptor){ .prop = "open < 0", .ident = errno };
  struct timernod *timroot = null;

  for (struct descriptor nq = { 0 };; swap (q, nq))
    {
      while (q.entry)
        {
          xapr *nd = xlist$ (shift) (&q.entry, &q.prop);
          auto k = container_of (nd, struct corosuperblk, lentry);
          while (k)
            switch (k->bindedfp (k))
              {
              case ENOMEM:;
                return (struct descriptor){ .prop = "bindedfp == ENOMEM",
                                            .ident = ENOMEM };
              case 0:;
                k = k->kont;
                break;
              case EAGAIN:;
                k = null;
                break;
              case ECHILD:;
                while ((k = k->kont))
                  xlist$ (push) (&k->lentry, &nq.entry, &nq.prop);
                break;
              case EIO:;
                {
                  struct iosrq *srq = k->srq;
                  struct epoll_event ev
                      = { .events = srq->events, .data.ptr = k };
                  if (epoll_ctl (epfd, EPOLL_CTL_ADD, srq->fd, &ev) < 0
                      && (errno != EEXIST
                          || epoll_ctl (epfd, EPOLL_CTL_MOD, srq->fd, &ev)
                                 < 0))
                    return (struct descriptor){ .prop = "epoll_ctl < 0",
                                                .ident = errno };
                  k = null;
                }
                break;
              case ETIMEDOUT:;
                {
                  struct timersrq *te = k->srq;
                  timroot = bst$ (ins) (timroot, &te->timernod, timercmp);
                  k = null;
                }
                break;
              }
        }
      int timeout = nq.entry ? 0 : -1;
      if (timroot && timeout < 0)
        {
          struct timersrq *te = timermin (&timroot);
          u64 now = monotime$ ();
          u64 delta = te->deadline > now ? te->deadline - now : 0;
          u64 ms = delta / 1000000 + (delta % 1000000 != 0);
          timeout = ms > INT_MAX ? INT_MAX : (int)ms;
        }
      int ne = epoll_wait (epfd, evs, BUFFER_SIZE, timeout);
      if (ne < 0)
        {
          if (errno == EINTR)
            continue;
          return (struct descriptor){ .prop = "epoll_wait < 0",
                                      .ident = errno };
        }
      for (int i = 0; i < ne; ++i)
        if (evs[i].data.ptr)
          {
            struct corosuperblk *k = evs[i].data.ptr;
            xlist$ (push) (&k->lentry, &nq.entry, &nq.prop);
          }
        else
          accept_spawn (sockfd, &nullfd, &nq, spawn);
      if (timroot)
        {
          u64 now = monotime$ ();
          xapr *prev = null, *next = nq.entry;
          for (struct timersrq *te;
               (te = timermin (&timroot)) && te->deadline <= now;)
            {
              timroot = bst$ (rem) (&te->timernod);
              xlist$ (ins) (&te->kont->lentry, prev, next, &nq.entry,
                            &nq.prop);
              prev = &te->kont->lentry;
            }
        }
    }
  unreachable$ ();
}

struct descriptor
socksrv (struct sockaddr *sockaddr, socklen_t sockaddrlen,
         struct descriptor initq, void *(*spawn) (struct sockpeer))
{
  signal (SIGPIPE, SIG_IGN);
  autofd int sockfd = socket (sockaddr->sa_family,
                              SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sockfd < 0)
    return (struct descriptor){ .prop = "socket < 0", .ident = errno };
  if (sockaddr->sa_family == AF_INET6
      && setsockopt (sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){ 0 },
                     sizeof (int))
             < 0)
    return (struct descriptor){ .prop = "setsockopt < 0", .ident = errno };
  if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof (int))
      < 0)
    return (struct descriptor){ .prop = "setsockopt < 0", .ident = errno };
  if (bind (sockfd, sockaddr, sockaddrlen))
    return (struct descriptor){ .prop = "bind < 0", .ident = errno };
  if (listen (sockfd, BUFFER_SIZE) < 0)
    return (struct descriptor){ .prop = "listen < 0", .ident = errno };
  return sched_loop (sockfd, initq, spawn);
}
