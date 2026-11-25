#include "server.h"

coroutine$ (void, root, {
  int connfd;
  byte buffer[4096];
  isz nr;
  isz nw;
});
static int
root (void *kf) async$ ((struct root *)kf)
{
  for (;;)
    {
      await$ ($.nr =, aread, $.connfd, $.buffer, countof ($.buffer));
      if ($.nr <= 0)
        break;
      batch$ (($.nw =, awrite, $.connfd, $.buffer, $.nr));
      if ($.nw != $.nr)
        break;
    }
  close ($.connfd);
  break$ (cofree$ (&$), EAGAIN);
}

void *
spawn_server (struct sockpeer sockpeer)
{
  int fd = sockpeer.fd;
  void *kf = coalloc$ (sizeof (struct root));
  if (!kf)
    {
      close (fd);
      return null;
    }
  memcpy (kf, &(struct root){ { .bindedfp = root }, .connfd = fd },
          sizeof (struct root));
  return kf;
}
