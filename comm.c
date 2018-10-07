/*
 * Copyright Brian Starkey <stark3y@gmail.com> 2018
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "comm.h"

struct comm {
	int socket;
	int connection;

	bool body;
	struct comm_packet current;
	uint8_t *cursor;
	size_t remaining;
};

static void __comm_reset(struct comm *comm)
{
	comm->cursor = (uint8_t *)&comm->current;
	comm->remaining = sizeof(comm->current.type) + sizeof(comm->current.length);
	free(comm->current.data);
	comm->current.data = NULL;
}

/*
 * From the gnu docs
 * https://www.gnu.org/software/libc/manual/html_node/Local-Socket-Example.html
 */
static int make_named_socket (const char *filename)
{
	struct sockaddr_un name;
	int sock;
	size_t size;

	/* Create the socket. */
	sock = socket (PF_LOCAL, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sock < 0)
	{
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	/* Bind a name to the socket. */
	name.sun_family = AF_LOCAL;
	strncpy (name.sun_path, filename, sizeof (name.sun_path));
	name.sun_path[sizeof (name.sun_path) - 1] = '\0';

	/* The size of the address is
	   the offset of the start of the filename,
	   plus its length (not including the terminating null byte).
	   Alternatively you can just do:
	   size = SUN_LEN (&name);
	   */
	size = (offsetof (struct sockaddr_un, sun_path)
			+ strlen (name.sun_path));
	if (bind (sock, (struct sockaddr *) &name, size) < 0)
	{
		perror ("bind");
		exit (EXIT_FAILURE);
	}

	return sock;
}

struct comm *comm_init_unix(const char *socket)
{
	int ret;
	struct comm *comm = calloc(1, sizeof(*comm));
	if (!comm) {
		return NULL;
	}
	comm->socket = -1;
	comm->connection = -1;

	if (!access(socket, F_OK)) {
		fprintf(stderr, "Socket '%s' already exists, removing.\n", socket);
		ret = unlink(socket);
		if (ret) {
			perror("Unlink failed:");
			goto fail;
		}
	}

	ret = make_named_socket(socket);
	if (ret < 0) {
		goto fail;
	}
	comm->socket = ret;

	fprintf(stderr, "Listening on socket %s (%d)\n", socket, comm->socket);
	listen(comm->socket, 10);

	__comm_reset(comm);

	return comm;

fail:
	if (comm->socket >= 0) {
		close(comm->socket);
	}
	free(comm);
	return NULL;
}

struct comm *comm_init_tcp(int port)
{
	struct sockaddr_in addr;
	struct comm *comm = calloc(1, sizeof(*comm));
	if (!comm) {
		return NULL;
	}
	comm->socket = -1;
	comm->connection = -1;

	comm->socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (comm->socket < 0)
	{
		perror ("socket");
		goto fail;
	}

	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = 0;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_family = AF_INET;

	if (bind (comm->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		perror ("bind");
		goto fail;
	}

	fprintf(stderr, "Listening on socket %d (%d)\n", port, comm->socket);
	listen(comm->socket, 10);

	__comm_reset(comm);

	return comm;

fail:
	if (comm->socket >= 0) {
		close(comm->socket);
	}
	free(comm);
	return NULL;
}

static int __comm_check_connection(struct comm *comm)
{
	int ret;
	if (comm->connection < 0) {
		ret = accept4(comm->socket, NULL, NULL, SOCK_NONBLOCK);
		if (ret == -1) {
			if (errno != EWOULDBLOCK && errno != EAGAIN) {
				perror("Unexpected error in accept():");
				return -1;
			}
			return 1;
		}
		comm->connection = ret;
	}

	return 0;
}

static int __comm_write(struct comm *comm, uint8_t *data, uint32_t len)
{
	int ret;
	while (len) {
		ret = write(comm->connection, data, len);
		if (ret < 0) {
			if (errno != EWOULDBLOCK && errno != EAGAIN) {
				fprintf(stderr, "__comm_write %s\n", strerror(errno));

				/* TODO: Is this correct? Think SIGPIPE will kill us */
				//close(comm->connection);
				//comm->connection = -1;
				return ret;
			}
		} else {
			len -= ret;
			data += ret;
			/* fprintf(stderr, "Send %d (of %d remaining)\n", ret, len); */
		}
		usleep(1000);
	}

	return len == 0 ? 0 : -1;
}

int comm_send(struct comm *comm, uint32_t type, uint32_t length, uint8_t *data)
{
	int ret;

	ret = __comm_check_connection(comm);
	if (ret > 0) {
		/* No connection. */
		return -EAGAIN;
	} else if (ret < 0) {
		return -1;
	}

	if (type != 0) {
		ret = __comm_write(comm, (uint8_t *)&type, sizeof(type));
		if (ret < 0) {
			fprintf(stderr, "__comm_send %d: %s\n", ret, strerror(errno));
			return ret;
		}

		ret = __comm_write(comm, (uint8_t *)&length, sizeof(length));
		if (ret < 0) {
			fprintf(stderr, "__comm_send %d: %s\n", ret, strerror(errno));
			return ret;
		}
	}

	ret = __comm_write(comm, data, length);
	if (ret < 0) {
		fprintf(stderr, "__comm_send %d: %s\n", ret, strerror(errno));
		return ret;
	}

	return 0;
}

int comm_poll(struct comm *comm, struct comm_packet **recv)
{
	int ret, npkts = 0;
	struct comm_packet *pkts = NULL;

	ret = __comm_check_connection(comm);
	if (ret > 0) {
		/* No connection. */
		return -EAGAIN;
	} else if (ret < 0) {
		return -1;
	}

	while (1) {
		ret = read(comm->connection, comm->cursor, comm->remaining);
		if (ret == 0) {
			/* Client went away - clean up, but no error */
			close(comm->connection);
			comm->connection = -1;

			/* Discard current state */
			__comm_reset(comm);
			break;
		} else if (ret == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				perror("Unexpected read error:");
				goto error;
			}
			break;
		} else if (ret == comm->remaining) {
			if (!comm->current.data) {
				/* We just received the header */
				comm->current.data = calloc(1, comm->current.length);
				comm->cursor = comm->current.data;
				comm->remaining = comm->current.length;
			} else {
				/* Packet finished */
				pkts = realloc(pkts, (npkts + 1) * sizeof(*pkts));

				pkts[npkts].type = comm->current.type;
				pkts[npkts].length = comm->current.length;
				pkts[npkts].data = comm->current.data;
				comm->current.data = NULL;

				npkts++;
				__comm_reset(comm);
			}
			/* Go around again. */
		} else {
			/* Short read */
			comm->remaining -= ret;
			comm->cursor += ret;
			break;
		}
	}

	*recv = pkts;

	return npkts;

error:
	if (comm->connection >= 0) {
		close(comm->connection);
		comm->connection = -1;
	}

	__comm_reset(comm);

	return -1;
}

void comm_free_packets(struct comm_packet *pkts, int npkts)
{
	int i;
	for (i = 0; i < npkts; i++) {
		free(pkts[i].data);
	}
	free(pkts);
}
