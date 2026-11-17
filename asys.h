#pragma once
#ifndef _H_ASYS_
#define _H_ASYS_ 1
#include <cmacs.h>

struct iosrq
{
  int fd;
  u32 events;
};

coroutine$ (isz, aread, {
  int fd;
  void *buffer;
  usz size;

  usz offset;
  struct iosrq srq;
});
int aread (void *);

coroutine$ (isz, awrite, {
  int fd;
  void const *buffer;
  usz size;

  usz offset;
  struct iosrq srq;
});
int awrite (void *);

bstnod$ (timernod, 0);
struct timersrq
{
  u64 deadline;
  struct timernod timernod;
  struct corosuperblk *kont;
};

coroutine$ (void, asleep, {
  u64 ms;

  struct timersrq srq;
});
int asleep (void *);

#endif /* _H_ASYS_ */
