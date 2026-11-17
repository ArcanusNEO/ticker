#pragma once
#ifndef _H_SOCKSRV_
#define _H_SOCKSRV_ 1

#include <cmacs.h>
#include "asys.h"
#include "misc.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>

struct sockpeer
{
  int fd;
  struct sockaddr_storage addr;
};

struct descriptor socksrv (struct sockaddr *sockaddr, socklen_t sockaddrlen,
                           struct descriptor initq,
                           void *(*spawn) (struct sockpeer));

#endif /* _H_SOCKSRV_ */
