#include "asys.h"
#include <liburing.h>

int
aread (void *kf) async$ ((struct aread *)kf)
{
  $.srq = (struct iosrq){
    .fd = $.fd, .op = IORING_OP_READ, .buffer = $.buffer, .size = $.size
  };
  isz n;
  do
    {
      $.mbr$ (super).srq = &$.srq;
      yield$ (EIO);
      n = $.mbr$ (super).error;
    }
  while (n == -EINTR);
  exit$ (n);
}

int
awrite (void *kf) async$ ((struct awrite *)kf)
{
  while ($.offset < $.size)
    {
      $.srq = (struct iosrq){ .fd = $.fd,
                              .op = IORING_OP_WRITE,
                              .buffer = (byte *)$.buffer + $.offset,
                              .size = $.size - $.offset };
      isz n;
      do
        {
          $.mbr$ (super).srq = &$.srq;
          yield$ (EIO);
          n = $.mbr$ (super).error;
        }
      while (n == -EINTR);
      if (n < 0)
        return$ (n);
      if (!n)
        break;
      $.offset += n;
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
