/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include "libutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libcompat.h>
#include <unistd.h>

#if defined(_WIN32)
#define  WINDOWS_MEAN_AND_LEAN
#include <Windows.h>
#endif

/*
    NAME
         pidfile_open, pidfile_write, pidfile_close, 
            pidfile_remove -- library for PID files handling.

    LIBRARY
         System Utilities Library (libutil, -lutil)

    SYNOPSIS
         #include <libutil.h>

         struct pidfh *
         pidfile_open(const char *path, mode_t mode, pid_t *pidptr);

         int
         pidfile_write(struct pidfh *pfh);

         int
         pidfile_close(struct pidfh *pfh);

         int
         pidfile_remove(struct pidfh *pfh);

         int
         pidfile_fileno(struct pidfh *pfh);

    DESCRIPTION
         The pidfile family of functions allows daemons to handle PID files. It
         uses flopen(3) to lock a pidfile and detect already running daemons.

         The pidfile_open() function opens (or creates) a file specified by the
         path argument and locks it.  If pidptr argument is not NULL and file can
         not be locked, the function will use it to store a PID of an already running
         daemon or -1 in case daemon did not write its PID yet.  The function
         does not write process' PID into the file here, so it can be used before
         fork()ing and exit with a proper error message when needed.  If the path
         argument is NULL, /var/run/<progname>.pid file will be used.  The
         pidfile_open() function sets the O_CLOEXEC close-on-exec flag when opening
         the pidfile.

         The pidfile_write() function writes process' PID into a previously opened
         file.  The file is truncated before write, so calling the pidfile_write()
         function multiple times is supported.

         The pidfile_close() function closes a pidfile.  It should be used after
         daemon fork()s to start a child process.

         The pidfile_remove() function closes and removes a pidfile.

         The pidfile_fileno() function returns the file descriptor for the open
         pidfile.

    RETURN VALUES
         The pidfile_open() function returns a valid pointer to a pidfh structure
         on success, or NULL if an error occurs.  If an error occurs, errno will
         be set.

         The pidfile_write(), pidfile_close(), and pidfile_remove() functions
         return the value 0 if successful; otherwise the value -1 is returned and
         the global variable errno is set to indicate the error.

         The pidfile_fileno() function returns the low-level file descriptor.  It
         returns -1 and sets errno if a NULL pidfh is specified, or if the pidfile
         is no longer open.

    EXAMPLES
         The following example shows in which order these functions should be
         used.  Note that it is safe to pass NULL to pidfile_write(),
         pidfile_remove(), pidfile_close() and pidfile_fileno() functions.

         struct pidfh *pfh;
         pid_t otherpid, childpid;

         pfh = pidfile_open("/var/run/daemon.pid", 0600, &otherpid);
         if (pfh == NULL) {
            if (errno == EEXIST) {
                errx(EXIT_FAILURE, "Daemon already running, pid: %jd.", (intmax_t)otherpid);

          }

          (* If we cannot create pidfile from other reasons, only warn. *)
          warn("Cannot open or create pidfile");

          (*
           * Even though pfh is NULL we can continue, as the other pidfile_*
           * function can handle such situation by doing nothing except setting
           * errno to EDOOFUS.
           *)
         }

        if (daemon(0, 0) == -1) {
            warn("Cannot daemonize");
            pidfile_remove(pfh);
            exit(EXIT_FAILURE);
         }

         pidfile_write(pfh);

         for (;;) {
            (* Do work. *)
            childpid = fork();
            switch (childpid) {
            case -1:
                syslog(LOG_ERR, "Cannot fork(): %s.", strerror(errno));
                break;
            case 0:
                pidfile_close(pfh);
                (* Do child work. *)
                break;
            default:
                syslog(LOG_INFO, "Child %jd started.", (intmax_t)childpid);
                break;
            }
         }

         pidfile_remove(pfh);
         exit(EXIT_SUCCESS);

    ERRORS
         The pidfile_open() function will fail if:

         [EEXIST]   Some process already holds the lock on the given pidfile, 
                    meaning that a daemon is already running.  If pidptr argument is not NULL
                    the function will use it to store a PID of an already running daemon or -1 in
                    case daemon did not write its PID yet.

         [ENAMETOOLONG] Specified pidfile's name is too long.

         [EINVAL]   Some process already holds the lock on the given pidfile, 
                    but PID read from there is invalid.

                    The pidfile_open() function may also fail and set errno for any errors
                    specified for the fstat(2), open(2), and read(2) calls.

                    The pidfile_write() function will fail if:

         [EDOOFUS]  Improper function use. Probably called before
                    pidfile_open().

                    The pidfile_write() function may also fail and set errno for any errors
                    specified for the fstat(2), ftruncate(2), and write(2) calls.

                    The pidfile_close() function may fail and set errno for any errors 
                    specified for the close(2) and fstat(2) calls.

                    The pidfile_remove() function will fail if:

         [EDOOFUS]  Improper function use. Probably called not from the process which 
                    made pidfile_write().

                    The pidfile_remove() function may also fail and set errno for any errors
                    specified for the close(2), fstat(2), write(2), and unlink(2) system
                    calls and the flopen(3) library function.

                    The pidfile_fileno() function will fail if:

         [EDOOFUS]  Improper function use. Probably called not from the
                    process which used pidfile_open().

    SEE ALSO
         open(2), daemon(3), flopen(3)

    AUTHORS
         The pidfile functionality is based on ideas from John-Mark Gurney
         <jmg@FreeBSD.org>.

         The code and manual page was written by Pawel Jakub Dawidek
         <pjd@FreeBSD.org>.
*/

struct pidfh {
#if !defined(_WIN32)
	int	pf_dirfd;
#endif
	int	pf_fd;
	char	pf_dir[MAXPATHLEN + 1];
	char	pf_filename[MAXPATHLEN + 1];
	dev_t	pf_dev;
	ino_t	pf_ino;
};

static int _pidfile_remove(struct pidfh *pfh, int freeit);

#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif
#if !defined(EDOOFUS)
#define EDOOFUS EINVAL
#endif

static int
pidfile_verify(const struct pidfh *pfh)
{
	struct stat sb;

	if (pfh == NULL || pfh->pf_fd == -1)
		return (EDOOFUS);
	/*
	 * Check remembered descriptor.
	 */
	if (fstat(pfh->pf_fd, &sb) == -1)
		return (errno);
	if (sb.st_dev != pfh->pf_dev || sb.st_ino != pfh->pf_ino)
		return (EDOOFUS);
	return (0);
}


#if !defined(_WIN32)
static int
pidfile_read(int dirfd, const char *filename, pid_t *pidptr)
{
	char buf[16], *endptr;
	int error, fd, i;

	fd = openat(dirfd, filename, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return (errno);

	i = read(fd, buf, sizeof(buf) - 1);
	error = errno;	/* Remember errno in case close() wants to change it. */
	close(fd);
	if (i == -1)
		return (error);
	else if (i == 0)
		return (EAGAIN);
	buf[i] = '\0';

	*pidptr = strtol(buf, &endptr, 10);
	if (endptr != &buf[i])
		return (EINVAL);

	return (0);
}
#endif  //_WIN32

struct pidfh *
pidfile_open(const char *pathp, mode_t mode, pid_t *pidptr)
{
	char path[MAXPATHLEN];
	struct pidfh *pfh;
	struct stat sb;
	int error, dirlen, filenamelen;
#if defined(_WIN32)
        int fd = -1;
#else
        int fd = -1, dirfd = -1, count;
#endif
//	struct timespec rqtp;
//	cap_rights_t caprights;

	pfh = malloc(sizeof(*pfh));
	if (pfh == NULL)
		return (NULL);

	if (pathp == NULL) {
		dirlen = snprintf(pfh->pf_dir, sizeof(pfh->pf_dir), "/var/run/");
		filenamelen = snprintf(pfh->pf_filename,
		    sizeof(pfh->pf_filename), "%s.pid", getprogname());
	} else {
		if (strlcpy(path, pathp, sizeof(path)) >= sizeof(path)) {
			free(pfh);
			errno = ENAMETOOLONG;
			return (NULL);
		}
		dirlen = strlcpy(pfh->pf_dir, dirname(path), sizeof(pfh->pf_dir));
		(void)strlcpy(path, pathp, sizeof(path));
		filenamelen = strlcpy(pfh->pf_filename, basename(path),
		    sizeof(pfh->pf_filename));
	}

	if (dirlen >= (int)sizeof(pfh->pf_dir) ||
	    filenamelen >= (int)sizeof(pfh->pf_filename) ||
	    (dirlen + filenamelen) >= (int)sizeof(pfh->pf_filename)) {
		free(pfh);
		errno = ENAMETOOLONG;
		return (NULL);
	}

#if defined(_WIN32)
	{	char t_path[MAXPATHLEN + 1];
		HANDLE handle = 0;

		(void) snprintf(t_path, sizeof(t_path), "%s/%s", pfh->pf_dir, pfh->pf_filename);
		handle = CreateFileA(
			    t_path,                 // name of the write.
			    GENERIC_WRITE,          // open for writing.
			    FILE_SHARE_READ,        // shared reading/exclusive write.
			    NULL,                   // default security.
			    OPEN_ALWAYS,            // create/open file.
			    FILE_ATTRIBUTE_NORMAL,  // normal file.
			    NULL);                  // no attr. template

		if (INVALID_HANDLE_VALUE == handle) { 
			return NULL;

		} else if (0xFFFFFFFF == SetFilePointer(handle, 0, NULL, FILE_CURRENT) ||
			    0xFFFFFFFF == SetFilePointer(handle, 0 /*TRUNCATE*/, NULL, FILE_BEGIN) ||
			    ! SetEndOfFile (handle) || 
			    -1 == (fd = _open_osfhandle((intptr_t)handle, O_WRONLY))) {
			CloseHandle(handle);
			errno = EIO;
			return (NULL);
		}
	}

#else
	dirfd = open(pfh->pf_dir, O_CLOEXEC | O_DIRECTORY | O_NONBLOCK);
	if (dirfd == -1) {
		error = errno;
		free(pfh);
		errno = error;
		return (NULL);
	}

	/*
	 * Open the PID file and obtain exclusive lock.
	 * We truncate PID file here only to remove old PID immediately,
	 * PID file will be truncated again in pidfile_write(), so
	 * pidfile_write() can be called multiple times.
	 */
	fd = flopenat(dirfd, pfh->pf_filename,
	    	O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NONBLOCK, mode);
	if (fd == -1) {
		if (errno == EWOULDBLOCK) {
			if (pidptr == NULL) {
				errno = EEXIST;
			} else {
				count = 20;
				rqtp.tv_sec = 0;
				rqtp.tv_nsec = 5000000;
				for (;;) {
					errno = pidfile_read(dirfd,
					    pfh->pf_filename, pidptr);
					if (errno != EAGAIN || --count == 0)
						break;
					nanosleep(&rqtp, 0);
				}
				if (errno == EAGAIN)
					*pidptr = -1;
				if (errno == 0 || errno == EAGAIN)
					errno = EEXIST;
			}
		}
		error = errno;
		close(dirfd);
		free(pfh);
		errno = error;
		return (NULL);
	}
#endif /*WIN32*/

	/*
	 * Remember file information, so in pidfile_write() we are sure we write
	 * to the proper descriptor.
	 */
	if (fstat(fd, &sb) == -1) {
		goto failed;
	}

//	if (cap_rights_limit(dirfd,
//		    cap_rights_init(&caprights, CAP_UNLINKAT)) < 0 && errno != ENOSYS) {
//		goto failed;
//	}

//	if (cap_rights_limit(fd, cap_rights_init(&caprights, CAP_PWRITE,
//		    CAP_FSTAT, CAP_FTRUNCATE, CAP_EVENT)) < 0 &&
//		    errno != ENOSYS) {
//		goto failed;
//	}

#if !defined(_WIN32)
	pfh->pf_dirfd = dirfd;
#endif
	pfh->pf_fd = fd;
	pfh->pf_dev = sb.st_dev;
	pfh->pf_ino = sb.st_ino;

	return (pfh);

failed:
	error = errno;
#if !defined(_WIN32)
	unlinkat(dirfd, pfh->pf_filename, 0);
	close(dirfd);
#endif
	if (fd >= 0) close(fd);
	free(pfh);
	errno = error;
	return (NULL);
}

int
pidfile_write(struct pidfh *pfh)
{
	char pidstr[16];
	int error, fd;

	/*
	 * Check remembered descriptor, so we don't overwrite some other
	 * file if pidfile was closed and descriptor reused.
	 */
	errno = pidfile_verify(pfh);
	if (errno != 0) {
		/*
		 * Don't close descriptor, because we are not sure if it's ours.
		 */
		return (-1);
	}
	fd = pfh->pf_fd;

	/*
	 * Truncate PID file, so multiple calls of pidfile_write() are allowed.
	 */
	if (ftruncate(fd, 0) == -1) {
		error = errno;
		_pidfile_remove(pfh, 0);
		errno = error;
		return (-1);
	}

	snprintf(pidstr, sizeof(pidstr), "%u", getpid());
	if (pwrite(fd, pidstr, strlen(pidstr), 0) != (ssize_t)strlen(pidstr)) {
		error = errno;
		_pidfile_remove(pfh, 0);
		errno = error;
		return (-1);
	}

	return (0);
}

int
pidfile_close(struct pidfh *pfh)
{
	int error;

	error = pidfile_verify(pfh);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	if (close(pfh->pf_fd) == -1)
		error = errno;
#if !defined(_WIN32)
	if (close(pfh->pf_dirfd) == -1 && error == 0)
		error = errno;
#endif

	free(pfh);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

static int
_pidfile_remove(struct pidfh *pfh, int freeit)
{
	int error;

	error = pidfile_verify(pfh);
	if (error != 0) {
		errno = error;
		return (-1);
	}

#if !defined(_WIN32)
	if (funlinkat(pfh->pf_dirfd, pfh->pf_filename, pfh->pf_fd, 0) == -1) {
		if (errno == EDEADLK)
			return (-1);
		error = errno;
	}
#endif
	if (close(pfh->pf_fd) == -1 && error == 0)
		error = errno;
#if !defined(_WIN32)
	if (close(pfh->pf_dirfd) == -1 && error == 0)
		error = errno;
#endif
	if (freeit)
		free(pfh);
	else
		pfh->pf_fd = -1;
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

int
pidfile_remove(struct pidfh *pfh)
{

	return (_pidfile_remove(pfh, 1));
}

int
pidfile_fileno(const struct pidfh *pfh)
{

	if (pfh == NULL || pfh->pf_fd == -1) {
		errno = EDOOFUS;
		return (-1);
	}
	return (pfh->pf_fd);
}

//end