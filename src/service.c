/*
 * service.c - Ecore_Con-compatible service handling.
 *
 * Copyright © 2009 Mikhail Gusarov <dottedmag@dottedmag.net>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "service.h"

#ifndef UNIX_PATH_MAX
#  define UNIX_PATH_MAX 108
#endif

/* Taken from libbb/make_directory.c */
static bool
create_directory(char *path, int mode)
{
    char *s = path;
    for (;;) {
        char saved_c = '\0';
        while (*s) {
            if (*s == '/') {
                while (*s == '/')
                    s++;
                saved_c = *s;
                *s = '\0';      /* Terminate string for mkdir below */
                break;
            } else
                s++;
        }

        if (mkdir(path, 0700) == -1) {
            struct stat st;
            if (errno != EEXIST || stat(path, &st) == -1)
                return false;

            if (!S_ISDIR(st.st_mode)) {
                errno = saved_c ? ENOTDIR : EEXIST;
                return false;
            }

            if (!saved_c)
                break;
        }

        if (!saved_c)
            break;

        *s = saved_c;
    }

    if (chmod(path, mode) == -1)
        return false;
    return true;
}

static bool
service_path(const char *name, int port, char *buf, int buflen)
{
    if (buflen == snprintf(buf, buflen, "/tmp/.ecore_service|%s|%d",
                           name, port))
        return false;

    return true;
}

static bool
prepare_path(const char *path)
{
    char *d = strdup(path);
    char *parent = dirname(d);

    if (!create_directory(parent, 0700)) {
        free(d);
        return false;
    }
    free(d);

    struct stat s;
    if (-1 == stat(path, &s)) {
        if (errno == ENOENT)
            return true;
        return false;
    }

    if (S_ISDIR(s.st_mode)) {
        if (-1 == rmdir(path))
            return errno == ENOENT;
        return true;
    } else {
        if (-1 == unlink(path))
            return errno == ENOENT;
        return true;
    }
}

int
service_listen(const char *app)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == -1)
        return -1;

    struct sockaddr_un addr = { AF_UNIX };
    if (!service_path(app, 0, addr.sun_path, UNIX_PATH_MAX - 1))
        goto err;

    if (!prepare_path(addr.sun_path))
        goto err;

    const int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        goto err;

    if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) == -1)
        goto err;

    if (listen(s, 127) == -1)
        goto err;

    return s;

    int saved_errno;
  err:
    saved_errno = errno;
    close(s);
    errno = saved_errno;
    return -1;
}
