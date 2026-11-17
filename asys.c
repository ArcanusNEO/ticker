#include "asys.h"
#include <sys/epoll.h>

int
aread (void *kf) async$ ((struct aread *)kf)
{
  $.srq = (struct iosrq){ $.fd, EPOLLIN | EPOLLET | EPOLLONESHOT };
  while ($.offset < $.size)
    {
      isz n = read ($.fd, (byte *)$.buffer + $.offset, $.size - $.offset);
      if (n > 0)
        {
          $.offset += n;
          break;
        }
      if (n == 0)
        break;
      if (errno == EINTR)
        continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        return$ (-errno);
      $.mbr$ (super).srq = &$.srq;
      yield$ (EIO);
    }
  exit$ ((isz)$.offset);
}

int
awrite (void *kf) async$ ((struct awrite *)kf)
{
  $.srq = (struct iosrq){ $.fd, EPOLLOUT | EPOLLET | EPOLLONESHOT };
  while ($.offset < $.size)
    {
      isz n
          = write ($.fd, (const byte *)$.buffer + $.offset, $.size - $.offset);
      if (n > 0)
        {
          $.offset += n;
          continue;
        }
      if (n == 0)
        break;
      if (errno == EINTR)
        continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        return$ (-errno);
      $.mbr$ (super).srq = &$.srq;
      yield$ (EIO);
    }
  exit$ ((isz)$.offset);
}

int
asleep (void *kf) async$ ((struct asleep *)kf)
{
  $.srq.deadline = monotime$ () + $.ms * 1000000;
  $.srq.kont = &$.mbr$ (super);
  $.mbr$ (super).srq = &$.srq;
  break$ (ETIMEDOUT);
}
