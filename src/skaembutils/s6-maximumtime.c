/* ISC license. */

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <skalibs/sgetopt.h>
#include <skalibs/uint.h>
#include <skalibs/error.h>
#include <skalibs/sig.h>
#include <skalibs/tai.h>
#include <skalibs/iopause.h>
#include <skalibs/selfpipe.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>

#define USAGE "s6-maximumtime [ -0 | -a | -b | -i | -k | -q | -t | -x | -1 | -2 ] milliseconds prog..."

int main (int argc, char const *const *argv, char const *const *envp)
{
  unsigned int timeout ;
  tain_t stamp, deadline ;
  iopause_fd x[1] = { { .fd = -1, .events = IOPAUSE_READ, .revents = 0 } } ;
  pid_t pid = 0 ;
  int tosend = SIGTERM ;
  PROG = "s6-maximumtime" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "0abikqtx12", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case '0': tosend = 0 ; break ;
        case 'a': tosend = SIGALRM ; break ;
        case 'b': tosend = SIGABRT ; break ;
        case 'i': tosend = SIGINT ; break ;
        case 'k': tosend = SIGKILL ; break ;
        case 'q': tosend = SIGQUIT ; break ;
        case 't': tosend = SIGTERM ; break ;
        case 'x': tosend = SIGXCPU ; break ;
        case '1': tosend = SIGUSR1 ; break ;
        case '2': tosend = SIGUSR2 ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if ((argc < 2) || !uint0_scan(argv[0], &timeout)) strerr_dieusage(100, USAGE) ;
  if (!timeout) timeout = 1 ;
  if (!tain_from_millisecs(&deadline, timeout))
    strerr_diefu1sys(111, "taia_from_millisecs") ;

  x[0].fd = selfpipe_init() ;
  if (x[0].fd < 0) strerr_diefu1sys(111, "selfpipe_init") ;
  
  if (selfpipe_trap(SIGCHLD) < 0) strerr_diefu1sys(111, "selfpipe_trap") ;

  pid = child_spawn0(argv[1], argv+1, envp) ;
  if (!pid)  strerr_diefu2sys(111, "spawn ", argv[1]) ;
  tain_now(&stamp) ;
  tain_add(&deadline, &deadline, &stamp) ;

  for (;;)
  {
    int r = iopause_stamp(x, 1, &deadline, &stamp) ;
    if (r < 0) strerr_diefu1sys(111, "iopause") ;
    if (!r) break ;
    if (x[0].revents & IOPAUSE_READ)
    {
      int cont = 1 ;
      while (cont)
      {
        switch (selfpipe_read())
        {
          case -1 : strerr_diefu1sys(111, "selfpipe_read") ;
          case 0 : cont = 0 ; break ;
          case SIGCHLD :
          {
            int wstat ;
            if (wait_pid_nohang(pid, &wstat) == pid)
            {
              if (WIFSIGNALED(wstat))
                strerr_diew1x(111, "child process crashed") ;
              else return WEXITSTATUS(wstat) ;
            }
          }
          default : strerr_diefu1x(101, "internal error, please submit a bug-report.") ;
        }
      }
    }
  }
  kill(pid, tosend) ;
  errno = ETIMEDOUT ;
  strerr_diewu1sys(99, "wait for child process") ;
}
