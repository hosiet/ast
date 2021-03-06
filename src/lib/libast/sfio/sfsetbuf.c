/***********************************************************************
 *                                                                      *
 *               This software is part of the ast package               *
 *          Copyright (c) 1985-2013 AT&T Intellectual Property          *
 *                      and is licensed under the                       *
 *                 Eclipse Public License, Version 1.0                  *
 *                    by AT&T Intellectual Property                     *
 *                                                                      *
 *                A copy of the License is available at                 *
 *          http://www.eclipse.org/org/documents/epl-v10.html           *
 *         (with md5 checksum b35adb5213ca9657e911e9befb180842)         *
 *                                                                      *
 *              Information and Software Systems Research               *
 *                            AT&T Research                             *
 *                           Florham Park NJ                            *
 *                                                                      *
 *               Glenn Fowler <glenn.s.fowler@gmail.com>                *
 *                    David Korn <dgkorn@gmail.com>                     *
 *                     Phong Vo <phongvo@gmail.com>                     *
 *                                                                      *
 ***********************************************************************/
#include "config_ast.h"  // IWYU pragma: keep

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sfhdr.h"
#include "sfio.h"

#include "ast.h"

#undef getpagesize

extern int getpagesize(void);

/*      Set a (new) buffer for a stream.
**      If size < 0, it is assigned a suitable value depending on the
**      kind of stream. The actual buffer size allocated is dependent
**      on how much memory is available.
**
**      Written by Kiem-Phong Vo.
*/

#define SF_TEST_read 0x01000

static_fn int sfsetlinemode(void) {
    static int modes = -1;

    if (modes >= 0) return modes;
    modes = 0;

    char *s;
    char *t;
    char *v;
    unsigned long b;
    size_t z;
    int n;
    int m;
    char buf[1024];

    /* modeled after the VMALLOC_OPTIONS parser -- lax on the syntax */
    /* copy option string to a writable buffer */
    t = getenv("SFIO_OPTIONS");
    if (!t) return modes;

    for (s = &buf[0], v = &buf[sizeof(buf) - 1]; s < v; ++s) {
        if ((*s = *t++) == 0) break;
    }
    *s = 0;

    for (s = buf;;) { /* skip blanks to option name */
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == ',') s++;
        if (*(t = s) == 0) break;

        v = NULL;
        while (*s) {
            if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == ',') {
                *s++ = 0; /* end of name */
                break;
            } else if (!v && *s == '=') {
                *s++ = 0; /* end of name */
                if (*(v = s) == 0) v = NULL;
            } else {
                s++;
            }
        }
        if ((t[0] == 'n' || t[0] == 'N') && (t[1] == 'o' || t[1] == 'O')) {
            t += 2;
            n = 0;
        } else {
            n = 1;
        }
        if (t[0] == 'S' && t[1] == 'F') {
            t += 2;
            if (t[0] == '_') t += 1;
        }
        switch (t[0]) {
            case 'L': /* SF_LINE */
            case 'l':
                if (n) {
                    modes |= SF_LINE;
                } else {
                    modes &= ~SF_LINE;
                }
                break;
            case 'M': /* maxrec maxmap */
            case 'm':
                if (n && v) {
                    z = (size_t)strton64(v, NULL, NULL, 0);
                } else {
                    z = 0;
                }
                while (1) {
                    char c = *++t;
                    if (c == 0) {
                        break;
                    } else if (c == 'm' || c == 'M') {  // max map
                        if (z) {
                            if (z != (size_t)SF_UNBOUND) z = roundof(z, _Sfpage);
                            _Sfmaxm = z;
                            _Sftest &= ~SF_TEST_read;
                        } else {
                            _Sftest |= SF_TEST_read;
                        }
                        break;
                    } else if (c == 'r' || c == 'R') {  // max red
                        _Sfmaxr = z;
                        break;
                    }
                }
                break;
            case 'T':
            case 't':
                if (v) {
                    do {
                        if (v[0] == 'n' && v[1] == 'o') {
                            v += 2;
                            m = !n;
                        } else {
                            m = n;
                        }
                        switch (v[0]) {
                            default:
                                if (isdigit(v[0])) {
                                    b = strton64(v, NULL, NULL, 0);
                                } else {
                                    b = 0;
                                }
                                break;
                        }
                        if (m) {
                            _Sftest |= b;
                        } else {
                            _Sftest &= ~b;
                        }
                    } while ((v = strchr(v, ',')) && ++v);
                }
                break;
            case 'W': /* SF_WCWIDTH */
            case 'w':
                if (n) {
                    modes |= SF_WCWIDTH;
                } else {
                    modes &= ~SF_WCWIDTH;
                }
                break;
            default: { break; }  // should this be abort()?
        }
    }

    return modes;
}

void *sfsetbuf(Sfio_t *f, const void *buf, size_t size) {
    int sf_malloc, oflags, init, okmmap, local;
    ssize_t bufsize, blksz;
    Sfdisc_t *disc;
    struct stat st;
    uchar *obuf = NULL;
    ssize_t osize = 0;
    SFMTXDECL(f)

    SFONCE()  // initialize mutexes
    SFMTXENTER(f, NULL)

    GETLOCAL(f, local);

    if (size == 0 && buf) { /* special case to get buffer info */
        _Sfi = f->val = (f->bits & SF_MMAP) ? (f->endb - f->data) : f->size;
        SFMTXRETURN(f, (void *)f->data)
    }

    /* cleanup actions already done, don't allow write buffering any more */
    if (_Sfexiting && !(f->flags & SF_STRING) && (f->mode & SF_WRITE)) {
        buf = NULL;
        size = 0;
    }

    if ((init = f->mode & SF_INIT)) {
        if (!f->pool && _sfsetpool(f) < 0) SFMTXRETURN(f, NULL)
    } else if ((f->mode & SF_RDWR) != SFMODE(f, local) && _sfmode(f, 0, local) < 0)
        SFMTXRETURN(f, NULL)

    if (init) {
        f->mode = (f->mode & SF_RDWR) | SF_LOCK;
    } else {
        int rv;

        /* make sure there is no hidden read data */
        if (f->proc && (f->flags & SF_READ) && (f->mode & SF_WRITE) &&
            _sfmode(f, SF_READ, local) < 0)
            SFMTXRETURN(f, NULL)

        /* synchronize first */
        SFLOCK(f, local)
        rv = SFSYNC(f);
        if (!local) SFOPEN(f)
        if (rv < 0) SFMTXRETURN(f, NULL)

        /* turn off the SF_SYNCED bit because buffer is changing */
        f->mode &= ~SF_SYNCED;
    }

    SFLOCK(f, local)

    if ((Sfio_t *)buf != f) {
        blksz = -1;
    } else /* setting alignment size only */
    {
        blksz = (ssize_t)size;

        if (!init) /* stream already initialized */
        {
            obuf = f->data;
            osize = f->size;
            goto done;
        } else /* initialize stream as if in the default case */
        {
            buf = NULL;
            size = (size_t)SF_UNBOUND;
        }
    }

    bufsize = 0;
    oflags = f->flags;

    /* see if memory mapping is possible (see sfwrite for SF_BOTH) */
    okmmap = (buf || (f->flags & SF_STRING) || (f->flags & SF_RDWR) == SF_RDWR) ? 0 : 1;

    /* save old buffer info */
    if (f->bits & SF_MMAP) {
        if (f->data) {
            if (f->getr && (f->mode & SF_GETR) && f->next) f->next[-1] = f->getr;
            SFMUNMAP(f, f->data, f->endb - f->data);
            f->data = NULL;
        }
    } else if (f->data == f->tiny) {
        f->data = NULL;
        f->size = 0;
    }
    obuf = f->data;
    osize = f->size;

    f->flags &= ~SF_MALLOC;
    f->bits &= ~SF_MMAP;
    f->mode &= ~SF_GETR;
    f->getr = 0;

    /* pure read/string streams must have a valid string */
    if ((f->flags & (SF_RDWR | SF_STRING)) == SF_RDSTR && (size == (size_t)SF_UNBOUND || !buf)) {
        size = 0;
    }

    /* set disc to the first discipline with a seekf */
    for (disc = f->disc; disc; disc = disc->disc) {
        if (disc->seekf) break;
    }

    if ((init || local) && !(f->flags & SF_STRING)) { /* ASSERT(f->file >= 0) */
        st.st_mode = 0;

        /* if has discipline, set size by discipline if possible */
        if (disc) {
            if ((f->here = SFSK(f, (Sfoff_t)0, SEEK_CUR, disc)) < 0) {
                goto unseekable;
            } else {
                Sfoff_t e;
                if ((e = SFSK(f, (Sfoff_t)0, SEEK_END, disc)) >= 0) {
                    f->extent = e > f->here ? e : f->here;
                }
                (void)SFSK(f, f->here, SEEK_SET, disc);
                goto setbuf;
            }
        }

        /* get file descriptor status */
        if (fstat(f->file, &st) < 0) {
            f->here = -1;
        } else {
#if _stat_blksize /* preferred io block size */
            f->blksz = (size_t)st.st_blksize;
#endif
            bufsize = 64 * 1024;
            if (S_ISDIR(st.st_mode) || (Sfoff_t)st.st_size < (Sfoff_t)SF_GRAIN) okmmap = 0;
            if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) {
                f->here = SFSK(f, (Sfoff_t)0, SEEK_CUR, f->disc);
            } else {
                f->here = -1;
            }

#if O_TEXT /* no memory mapping with O_TEXT because read()/write() alter data stream */
            if (okmmap && f->here >= 0 && (fcntl((int)f->file, F_GETFL, 0) & O_TEXT)) okmmap = 0;
#endif
        }

        /* set page size, this is also the desired default buffer size */
        if (_Sfpage <= 0) {
            if ((_Sfpage = (size_t)getpagesize()) <= 0) _Sfpage = SF_PAGE;
        }

        if (init) f->flags |= sfsetlinemode();

        if (f->here >= 0) {
            f->extent = (Sfoff_t)st.st_size;

            /* seekable std-devices are share-public by default */
            if (f == sfstdin || f == sfstdout || f == sfstderr) f->flags |= SF_SHARE | SF_PUBLIC;
        } else {
        unseekable:
            f->extent = -1;
            f->here = 0;

            if (init) {
                if (S_ISCHR(st.st_mode)) {
                    int oerrno = errno;

                    bufsize = SF_GRAIN;

                    /* set line mode for terminals */
                    if (!(f->flags & (SF_LINE | SF_WCWIDTH)) && isatty(f->file)) {
                        f->flags |= SF_LINE | SF_WCWIDTH;
                    } else /* special case /dev/null */
                    {
                        int dev, ino;
                        static int null_checked, null_dev, null_ino;
                        dev = (int)st.st_dev;
                        ino = (int)st.st_ino;
                        if (!null_checked) {
                            if (stat(DEVNULL, &st) < 0) {
                                null_checked = -1;
                            } else {
                                null_checked = 1;
                                null_dev = (int)st.st_dev;
                                null_ino = (int)st.st_ino;
                            }
                        }
                        if (null_checked >= 0 && dev == null_dev && ino == null_ino) SFSETNULL(f);
                    }
                    errno = oerrno;
                }

                /* initialize side buffer for r+w unseekable streams */
                if (!f->proc && (f->bits & SF_BOTH)) (void)_sfpopen(f, -1, -1, 1);
            }
        }
    }

    if (okmmap && size && (f->mode & SF_READ) &&
        f->extent >= 0) { /* see if we can try memory mapping */
        if (!disc) {
            for (disc = f->disc; disc; disc = disc->disc) {
                if (disc->readf) break;
            }
        }
        if (!disc) {
            if (!(_Sftest & SF_TEST_read)) f->bits |= SF_MMAP;
            if (size == (size_t)SF_UNBOUND) {
                if (bufsize > _Sfpage) {
                    size = bufsize * SF_NMAP;
                } else {
                    size = _Sfpage * SF_NMAP;
                }
                if (size > 256 * 1024) size = 256 * 1024;
            }
        }
    }

    /* get buffer space */
setbuf:
    if (size == (size_t)SF_UNBOUND) { /* define a default size suitable for block transfer */
        if (init && osize > 0) {
            size = osize;
        } else if (f == sfstderr && (f->mode & SF_WRITE)) {
            size = 0;
        } else if (f->flags & SF_STRING) {
            size = SF_GRAIN;
        } else if ((f->flags & SF_READ) && !(f->bits & SF_BOTH) && f->extent > 0 &&
                   f->extent < (Sfoff_t)_Sfpage) {
            size = (((size_t)f->extent + SF_GRAIN - 1) / SF_GRAIN) * SF_GRAIN;
        } else if ((ssize_t)(size = _Sfpage) < bufsize) {
            size = bufsize;
        }

        buf = NULL;
    }

    sf_malloc = 0;
    if (size > 0 && !buf && !(f->bits & SF_MMAP)) { /* try to allocate a buffer */
        if (obuf && size == (size_t)osize && init) {
            buf = (void *)obuf;
            obuf = NULL;
            sf_malloc = (oflags & SF_MALLOC);
        }
        if (!buf) {  // do allocation
            while (!buf && size > 0) {
                buf = malloc(size);
                if (buf) break;
                size /= 2;
            }
            if (size > 0) sf_malloc = SF_MALLOC;
        }
    }

    if (size == 0 && !(f->flags & SF_STRING) && !(f->bits & SF_MMAP) &&
        (f->mode & SF_READ)) { /* use the internal buffer */
        size = sizeof(f->tiny);
        buf = (void *)f->tiny;
    }

    /* set up new buffer */
    f->size = size;
    f->next = f->data = f->endr = f->endw = (uchar *)buf;
    f->endb = (f->mode & SF_READ) ? f->data : f->data + size;
    if (f->flags & SF_STRING) { /* these fields are used to test actual size - see sfseek() */
        f->extent = (!sf_malloc && ((f->flags & SF_READ) || (f->bits & SF_BOTH))) ? size : 0;
        f->here = 0;

        /* read+string stream should have all data available */
        if ((f->mode & SF_READ) && !sf_malloc) f->endb = f->data + size;
    }

    f->flags = (f->flags & ~SF_MALLOC) | sf_malloc;

    if (obuf && (obuf != f->data) && (osize > 0) && (oflags & SF_MALLOC)) {
        free(obuf);
        obuf = NULL;
    }

done:
    _Sfi = f->val = obuf ? osize : 0;

    /* blksz is used for aligning disk block boundary while reading data to
    ** optimize data transfer from disk (eg, via direct I/O). blksz can be
    ** at most f->size/2 so that data movement in buffer can be optimized.
    ** blksz should also be a power-of-2 for optimal disk seeks.
    */
    if (blksz <= 0 || (blksz & (blksz - 1)) != 0) blksz = SF_GRAIN;
    while (blksz > f->size / 2) blksz /= 2;
    f->blksz = blksz;

    if (!local) SFOPEN(f)

    SFMTXRETURN(f, (void *)obuf)
}
