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
#ifndef __COMM_H__
#define __COMM_H__
#include <stdint.h>

struct comm;

struct comm_packet {
	uint32_t type;
	uint32_t length;
	uint8_t *data;
};

struct comm *comm_init_unix(const char *socket);
struct comm *comm_init_tcp(int port);

/* If type == 0, just send data. data can be NULL to send header only */
int comm_send(struct comm *comm, uint32_t type, uint32_t length, uint8_t *data);
int comm_poll(struct comm *comm, struct comm_packet **recv);
void comm_free_packets(struct comm_packet *pkts, int npkts);

#endif /* __COMM_H__ */
