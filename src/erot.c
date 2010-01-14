/*
 * service.c - Ecore_Con-compatible service handling.
 *
 * Copyright © 2009 Alexander Kerner <lunohod@openinkpot.org>
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

#include <stdlib.h>
#include <err.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <xcb/randr.h>
#include <xcb/xcb_aux.h>

#include "service.h"

#define ROTATE "Rotate"
#define FORWARD_ROTATE "RotateForward"
#define BACK_ROTATE "RotateBack"
#define EXIT "Exit"

#define CONFIG_FILE "/etc/default/erot"

static xcb_connection_t *c;
static xcb_window_t root;
static bool randr_init;

static void
wait_for_input_or_x11_closed(int fd)
{
    struct pollfd fds[] = {
        {xcb_get_file_descriptor(c), POLLIN},
        {fd, POLLIN},
    };

    if (poll(fds, 2, -1) == -1)
        err(1, "Unable to pol file descriptors");

    if (fds[0].revents & POLLHUP) {
        warnx("X connection closed. Hanging up.");
        exit(0);
    }
}

static int
get_rotation_type()
{
    int i = 360;
    FILE *f = fopen(CONFIG_FILE, "r");
    if (f != NULL) {
        fscanf(f, "%d", &i);
        fclose(f);
    }

    return i;
}

static void
init_randr()
{
    if (!randr_init) {
        const xcb_query_extension_reply_t *qreply
            = xcb_get_extension_data(c, &xcb_randr_id);
        if (!qreply)
            errx(1, "No RandR extension found\n");

        xcb_randr_query_version_cookie_t cookie
            = xcb_randr_query_version(c, 1, 1);

        xcb_generic_error_t *e;
        xcb_randr_query_version_reply_t *reply
            = xcb_randr_query_version_reply(c, cookie, &e);

        if (!reply)
            errx(1, "Error querying RandR version: %d\n", e->error_code);

        if (reply->major_version < 1 || reply->minor_version < 1) {
            errx(1,
                 "Server's highest supported version (%d.%d) is lower than 1.1\n",
                 reply->major_version, reply->minor_version);
        }

        free(reply);
        randr_init = true;
    }
}

/*
 * Returned info is to be free(3)d.
 */
static xcb_randr_get_screen_info_reply_t *
get_screen_info()
{
    init_randr();

    xcb_randr_get_screen_info_cookie_t cookie
        = xcb_randr_get_screen_info(c, root);

    xcb_generic_error_t *e;
    xcb_randr_get_screen_info_reply_t *reply
        = xcb_randr_get_screen_info_reply(c, cookie, &e);

    if (!reply)
        err(1, "Error querying RandR screen orientation: %d\n",
            e->error_code);

    return reply;
}

/*
 * Will return true if rotated succesfully.
 * Will return false if state is out of date.
 * Will die otherwise.
 */
static bool
set_rotation(xcb_randr_rotation_t rotation,
             xcb_randr_get_screen_info_reply_t * cur_state)
{
    xcb_randr_set_screen_config_cookie_t cookie
        = xcb_randr_set_screen_config(c, root, XCB_CURRENT_TIME,
                                      cur_state->config_timestamp,
                                      cur_state->sizeID, rotation, 0);

    xcb_generic_error_t *e;
    xcb_randr_set_screen_config_reply_t *reply
        = xcb_randr_set_screen_config_reply(c, cookie, &e);

    if (!reply) {
        if (e->error_code == XCB_RANDR_SET_CONFIG_INVALID_CONFIG_TIME) {
            free(e);
            return false;
        }
        err(1, "Error setting screen rotation through RandR: %d\n",
            e->error_code);
    }
    free(reply);
    return true;
}

inline static int
randr_rot_to_degrees(xcb_randr_rotation_t rot)
{
    int ret = 0;
    while (rot >>= 1) {
        ret += 90;
    }
    return ret;
}

inline static xcb_randr_rotation_t
degrees_to_randr_rot(int rot)
{
    return 1 << (rot / 90);
}

static int
next_rotation(int current, int rotation_type, int dir)
{
    if (rotation_type == 360)
        return (current + 90 * dir + 360) % 360;

    /* Switch between 0 and rotation_type */
    if (current == 0)
        return rotation_type;
    else
        return 0;
}

static xcb_randr_rotation_t
next_rotation_randr(xcb_randr_rotation_t cur, int rotation_type, int dir)
{
    return
        degrees_to_randr_rot(next_rotation
                             (randr_rot_to_degrees(cur), rotation_type,
                              dir));
}

static void
rotate(int dir)
{
    int rotation_type = get_rotation_type();

    xcb_randr_get_screen_info_reply_t *r = get_screen_info();
    xcb_randr_rotation_t next_rotation
        = next_rotation_randr(r->rotation, rotation_type, dir);

    while (!set_rotation(next_rotation, r)) {
        free(r);
        r = get_screen_info();
        next_rotation =
            next_rotation_randr(r->rotation, rotation_type, dir);
    }

    free(r);
}

static int
accept_client(int fd)
{
    wait_for_input_or_x11_closed(fd);

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd == -1)
        warn("Unable to accept client connection");
    return client_fd;
}

#define MAX_BUFLEN 32

static bool
handle_client(int fd)
{
    int buflen = 0;
    char buf[MAX_BUFLEN] = { };

    for (;;) {
        wait_for_input_or_x11_closed(fd);

        int read_ = read(fd, buf + buflen, MAX_BUFLEN - buflen);

        if (read_ == -1) {
            warn("Unable to read data from client");
            close(fd);
            return true;
        } else if (read_ == 0) {
            if (!strcmp(buf, ROTATE))
                rotate(1);
            else if (!strcmp(buf, FORWARD_ROTATE))
                rotate(1);
            else if (!strcmp(buf, BACK_ROTATE))
                rotate(-1);
            else if (!strcmp(buf, EXIT)) {
                close(fd);
                return false;
            } else
                warnx("Unknown command received: %s", buf);
            close(fd);
            return true;
        }
    }
}

/* Simple single-client server */
static void
run()
{
    int service_fd = service_listen("erot");
    while (handle_client(accept_client(service_fd)));
}

int
main()
{
    int default_screen_num;
    c = xcb_connect(NULL, &default_screen_num);
    if (xcb_connection_has_error(c))
        errx(1, "cannot open display %s\n", getenv("DISPLAY"));

    xcb_screen_t *screen = xcb_aux_get_screen(c, default_screen_num);
    if (!screen)
        errx(1, "cannot obtain default screen\n");
    root = screen->root;

    xcb_prefetch_extension_data(c, &xcb_randr_id);

    run();

    xcb_disconnect(c);

    return 0;
}

#if 0
int
main()
{
    int i;
    int j = 1;
    printf("randr_rot_to_degrees\n");
    for (i = 0; i < 4; ++i) {
        printf("%d -> %d\n", j, randr_rot_to_degrees(j));
        j <<= 1;
    }
    printf("degrees_to_randr_rot\n");
    for (i = 0; i < 4; ++i)
        printf("%d -> %d\n", i * 90, degrees_to_randr_rot(i * 90));
    int k;
    for (i = 0; i <= 360; i += 90) {
        printf("rottype: %d\n", i);
        for (j = 0; j < 360; j += 90)
            for (k = -1; k <= 1; k += 2) {
                printf("%d -(%d)-> %d\n", j, k, next_rotation(j, i, k));
            }
    }
    return 0;
}
#endif
