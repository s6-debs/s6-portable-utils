/* ISC license. */

#include <errno.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/sgetopt.h>
#include <skalibs/bytestr.h>
#include <skalibs/uint.h>
#include <skalibs/buffer.h>
#include <skalibs/siovec.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>

#define USAGE "s6-head [ -S ] [ -1..9 | -n lines | -c chars ] [ file... ]"
#define dieusage() strerr_dieusage(100, USAGE)

typedef int headfunc_t (int, unsigned int) ;
typedef headfunc_t *headfunc_t_ref ;

static int dolines (int fd, unsigned int lines)
{
  char buf[BUFFER_INSIZE] ;
  buffer in = BUFFER_INIT(&buffer_read, fd, buf, BUFFER_INSIZE) ;
  buffer out = BUFFER_INIT(&buffer_write, 1, buf, BUFFER_INSIZE) ;
  siovec_t v[2] ;
  while (lines)
  {
    unsigned int w = 0 ;
    register int r = buffer_fill(&in) ;
    if (r <= 0) return !r ;
    out.c.n = in.c.n ; out.c.p = in.c.p ;
    buffer_rpeek(&in, v) ;
    for (;;)
    {
      unsigned int n = siovec_len(v, 2) ;
      register unsigned int i ;
      if (!n) break ;
      i = siovec_bytechr(v, 2, '\n') ;
      if (i < n)
      {
        w += i+1 ;
        siovec_seek(v, 2, i+1) ;
        if (!--lines)
        {
          out.c.n = (out.c.p + w) % out.c.a ;
          break ;
        }
      }
      else siovec_seek(v, 2, i) ;
    }
    if (!buffer_flush(&out)) return 0 ;
    in.c.n = out.c.n ; in.c.p = out.c.p ;
  }
  return 1 ;
}

static int safedolines (int fd, unsigned int lines)
{
  char tmp[lines] ;
  while (lines)
  {
    unsigned int r = allread(fd, tmp, lines) ;
    if ((r < lines) && (errno != EPIPE)) return 0 ;
    lines -= byte_count(tmp, r, '\n') ;
    if (buffer_put(buffer_1, tmp, r) < (int)r) return 0 ;
  }
  if (!buffer_flush(buffer_1)) return 0 ;
  return 1 ;
}

static int safedochars (int fd, unsigned int chars)
{
  return (fd_catn(fd, 1, chars) >= chars) ;
}

int main (int argc, char const *const *argv)
{
  headfunc_t_ref f ;
  unsigned int lines = 10 ;
  int islines = 1, safe = 0 ;
  PROG = "s6-head" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    int done = 0 ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "S123456789n:c:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'S' : safe = 1 ; break ;
        case '1' :
        case '2' :
        case '3' :
        case '4' :
        case '5' :
        case '6' :
        case '7' :
        case '8' :
        case '9' :
        {
          if (done) dieusage() ;
          islines = 1 ;
          lines = opt - '0' ;
          done = 1 ;
          break ;
        }
        case 'n' :
        {
          if (done || !uint0_scan(l.arg, &lines))
            strerr_dieusage(100, USAGE) ;
          islines = 1 ;
          done = 1 ;
          break ;
        }
        case 'c' :
        {
          if (done || !uint0_scan(l.arg, &lines))
            strerr_dieusage(100, USAGE) ;
          islines = 0 ;
          done = 1 ;
          break ;
        }
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (argc) safe = 0 ;
  f = islines ? safe ? &safedolines : &dolines : &safedochars ;
  if (!argc)
  {
    if (!(*f)(0, lines))
      strerr_diefu1sys(111, "head stdin") ;
  }
  else
  {
    unsigned int i = 0 ;
    for (; argv[i] ; i++)
    {
      int fd ;
      if (argc >= 2)
      {
        if (i) buffer_putnoflush(buffer_1, "\n", 1) ;
        buffer_putnoflush(buffer_1, "==> ", 4) ;
        if ((buffer_puts(buffer_1, argv[i]) <= 0)
         || (buffer_putflush(buffer_1, " <==\n", 5) < 0))
          strerr_diefu1sys(111, "write to stdout") ;
      }
      if ((argv[i][0] == '-') && !argv[i][1]) fd = 0 ;
      else fd = open_readb(argv[i]) ;
      if (fd == -1)
        strerr_diefu3sys(111, "open ", argv[i], " for reading") ;
      if (!(*f)(fd, lines))
        strerr_diefu2sys(111, "head ", argv[i]) ;
      fd_close(fd) ;
    }
  }
  return 0 ;
}
