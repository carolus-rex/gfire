/*
 * purple - Xfire Protocol Plugin
 *
 * This file is part of Gfire.
 *
 * See the AUTHORS file distributed with Gfire for a full list of
 * all contributors and this files copyright holders.
 *
 * Gfire is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gfire.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _GF_P2P_SESSION_H
#define _GF_P2P_SESSION_H

typedef struct _gfire_p2p_session gfire_p2p_session;

#include "gf_base.h"
#include "gf_buddies.h"
#include "gf_p2p.h"
#include "gf_p2p_dl_handler.h"
#include "gf_filetransfer.h"

typedef enum _gfire_p2p_session_addr_type {
	GF_P2P_ADDR_TYPE_LOCAL = 0,
	GF_P2P_ADDR_TYPE_EXTERN = 1,
	GF_P2P_ADDR_TYPE_USE = 2
} gfire_p2p_session_addr_type;

struct _gfire_p2p_session
{
	guint8 *moniker_self;
	guint8 *moniker_peer;

	struct sockaddr_in peer_addr[3];
	guint32 peer_natType;

	gfire_p2p_connection *con;

	guint32 sessid;
	guint32 seqid;

	// File Transfers
	GList *transfers;

	// Timeout handling
	//   Ping
	gboolean need_pong;
	guint8 ping_retries;
	//   Keep-Alive
	gboolean need_keep_alive;
	guint8 keep_alive_retries;

	// Received seqIDs
	gfire_bitlist *rec_seqids;

	gboolean init;

	guint check_timer;
	glong last_ping;
	glong last_keep_alive;

	gboolean connected;

	gfire_buddy *buddy;
};

// Creation and initialization
gfire_p2p_session *gfire_p2p_session_create(gfire_buddy *p_buddy, const gchar *p_salt);
void gfire_p2p_session_free(gfire_p2p_session *p_session, gboolean p_local_reason);

void gfire_p2p_session_bind(gfire_p2p_session *p_session, gfire_p2p_connection *p_p2p);
void gfire_p2p_session_set_addr(gfire_p2p_session *p_session, gfire_p2p_session_addr_type p_type, guint32 p_ip, guint16 p_port);
void gfire_p2p_session_start(gfire_p2p_session *p_session, guint32 p_natType);

// Getting information
const guint8 *gfire_p2p_session_get_moniker_peer(const gfire_p2p_session *p_session);
const guint8 *gfire_p2p_session_get_moniker_self(const gfire_p2p_session *p_session);

guint32 gfire_p2p_session_get_peer_ip(const gfire_p2p_session *p_session, gfire_p2p_session_addr_type p_type);
guint16 gfire_p2p_session_get_peer_port(const gfire_p2p_session *p_session, gfire_p2p_session_addr_type p_type);
const struct sockaddr_in *gfire_p2p_session_get_peer_addr(const gfire_p2p_session *p_session, gfire_p2p_session_addr_type p_type);

gfire_buddy *gfire_p2p_session_get_buddy(const gfire_p2p_session *p_session);

gboolean gfire_p2p_session_connected(const gfire_p2p_session *p_session);

// Sending
gboolean gfire_p2p_session_can_send(const gfire_p2p_session *p_session);
void gfire_p2p_session_send_data16_packet(gfire_p2p_session *p_session, const guint8 *p_data, guint16 p_len, const gchar *p_category);
void gfire_p2p_session_send_data32_packet(gfire_p2p_session *p_session, const guint8 *p_data, guint32 p_len, const gchar *p_category);

// Identifying
gboolean gfire_p2p_session_is_by_moniker_peer(const gfire_p2p_session *p_session, const guint8 *p_moniker);
gboolean gfire_p2p_session_is_by_moniker_self(const gfire_p2p_session *p_session, const guint8 *p_moniker);

// Data handling
void gfire_p2p_session_ping(gfire_p2p_session *p_session);
void gfire_p2p_session_pong(gfire_p2p_session *p_session);
void gfire_p2p_session_keep_alive_request(gfire_p2p_session *p_session);
void gfire_p2p_session_keep_alive_response(gfire_p2p_session *p_session);
gboolean gfire_p2p_session_handle_data(gfire_p2p_session *p_session, guint32 p_type, guint32 p_seqid, void *p_data, guint32 p_len, const gchar *p_category);

// File Transfers
void gfire_p2p_session_add_file_transfer(gfire_p2p_session *p_session, PurpleXfer *p_xfer);
void gfire_p2p_session_add_recv_file_transfer(gfire_p2p_session *p_session, gfire_filetransfer *p_transfer);
void gfire_p2p_session_remove_file_transfer(gfire_p2p_session *p_session, gfire_filetransfer *p_transfer, gboolean p_local_reason);
gfire_filetransfer *gfire_p2p_session_find_file_transfer(gfire_p2p_session *p_session, guint32 p_fileid);

#endif // _GF_P2P_SESSION_H
