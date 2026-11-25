#include "socksrv.h"
#include <liburing.h>
#include <poll.h>

#define TYPICAL_SIZE 4096

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

static struct io_uring_sqe *
getsqe (struct io_uring *ring)
{
  struct io_uring_sqe *sqe = io_uring_get_sqe (ring);
  if (sqe || io_uring_submit (ring) < 0)
    return sqe;
  return io_uring_get_sqe (ring);
}

static void
ringexit (struct io_uring **ringp)
{
  io_uring_queue_exit (*ringp);
}

static struct descriptor
sched_loop (int sockfd, struct descriptor q, void *(*spawn) (struct sockpeer))
{
  struct io_uring ring;
  int ret = io_uring_queue_init (TYPICAL_SIZE, &ring, 0);
  if (ret < 0)
    return (struct descriptor){ .prop = "io_uring_queue_init < 0",
                                .ident = -ret };
  defer$ (ringexit) struct io_uring *ringp = &ring;
  autofd int nullfd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
  if (nullfd < 0)
    return (struct descriptor){ .prop = "open < 0", .ident = errno };
  struct io_uring_sqe *sqe = getsqe (&ring);
  if (!sqe)
    return (struct descriptor){ .prop = "io_uring_get_sqe == null",
                                .ident = ENOMEM };
  io_uring_prep_poll_add (sqe, sockfd, POLLIN);
  io_uring_sqe_set_data (sqe, null);

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
                  struct io_uring_sqe *sqe = getsqe (&ring);
                  if (!sqe)
                    return (struct descriptor){ .prop
                                                = "io_uring_get_sqe == null",
                                                .ident = ENOMEM };
                  switch (srq->op)
                    {
                    case IORING_OP_READ:;
                      io_uring_prep_read (sqe, srq->fd, srq->buffer, srq->size,
                                          -1);
                      break;
                    case IORING_OP_WRITE:;
                      io_uring_prep_write (sqe, srq->fd, srq->buffer,
                                           srq->size, -1);
                      break;
                    default:;
                      return (struct descriptor){ .prop = "srq->op invalid",
                                                  .ident = EINVAL };
                    }
                  io_uring_sqe_set_data (sqe, k);
                  k = null;
                }
                break;
              case ETIMEDOUT:;
                {
                  struct timersrq *te = k->srq;
                  struct io_uring_sqe *sqe = getsqe (&ring);
                  if (!sqe)
                    return (struct descriptor){ .prop
                                                = "io_uring_get_sqe == null",
                                                .ident = ENOMEM };
                  u64 now = monotime$ ();
                  /* the timeout value is inlined into the SQE as
                     nanoseconds relative to the current monotonic clock */
                  io_uring_prep_timeout (
                      sqe,
                      (void *)(uptr)(te->deadline > now ? te->deadline - now
                                                        : 0),
                      0, IORING_TIMEOUT_IMMEDIATE_ARG);
                  io_uring_sqe_set_data (sqe, te->kont);
                  k = null;
                }
                break;
              }
        }
      do
        ret = io_uring_submit_and_wait (&ring, nq.entry ? 0 : 1);
      while (ret == -EINTR);
      if (ret < 0)
        return (struct descriptor){ .prop = "io_uring_submit_and_wait < 0",
                                    .ident = -ret };
      struct io_uring_cqe *cqe;
      while (!io_uring_peek_cqe (&ring, &cqe))
        {
          struct corosuperblk *k = io_uring_cqe_get_data (cqe);
          if (k)
            {
              k->error = cqe->res;
              xlist$ (push) (&k->lentry, &nq.entry, &nq.prop);
            }
          else
            {
              accept_spawn (sockfd, &nullfd, &nq, spawn);
              struct io_uring_sqe *sqe = getsqe (&ring);
              if (!sqe)
                return (struct descriptor){ .prop = "io_uring_get_sqe == null",
                                            .ident = ENOMEM };
              io_uring_prep_poll_add (sqe, sockfd, POLLIN);
              io_uring_sqe_set_data (sqe, null);
            }
          io_uring_cqe_seen (&ring, cqe);
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
  if (listen (sockfd, TYPICAL_SIZE) < 0)
    return (struct descriptor){ .prop = "listen < 0", .ident = errno };
  return sched_loop (sockfd, initq, spawn);
}
