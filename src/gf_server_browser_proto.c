/*
 * purple - Xfire Protocol Plugin
 *
 * Copyright (C) 2000-2001, Beat Wolf <asraniel@fryx.ch>
 * Copyright (C) 2006,      Keith Geffert <keith@penguingurus.com>
 * Copyright (C) 2008-2009, Laurent De Marez <laurentdemarez@gmail.com>
 * Copyright (C) 2009       Warren Dumortier <nwarrenfl@gmail.com>
 * Copyright (C) 2009       Oliver Ney <oliver@dryder.de>
 *
 * This file is part of Gfire.
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

#include "gf_server_browser_proto.h"
#include "gf_server_browser.h"

#ifdef HAVE_GTK
static gfire_server_browser *server_browser;

// Socket
static int server_browser_socket;
static int server_browser_socket_handle;
static int server_browser_event_src;

// Queues
static GQueue *server_browser_queue;
static GQueue *server_browser_queried_queue;

// Protocol queries
#define SOURCE_QUERY "\xFF\xFF\xFF\xFF\x54Source Engine Query"
#define Q3_QUERY "\xFF\xFF\xFF\xFFgetstatus"

static GList *server_browser_proto_get_favorite_servers(guint32 p_gameid);

void gfire_server_browser_proto_parse_packet_q3();
void gfire_server_browser_proto_parse_packet_source();

// Creation & freeing
gfire_server_browser_server *gfire_server_browser_server_new()
{
	gfire_server_browser_server *ret;
	ret = (gfire_server_browser_server *)g_malloc0(sizeof(gfire_server_browser_server));

	return ret;
}

gfire_server_browser_server_info *gfire_server_browser_server_info_new()
{
	gfire_server_browser_server_info *ret;
	ret = (gfire_server_browser_server_info *)g_malloc0(sizeof(gfire_server_browser_server_info));

	return ret;
}

static gfire_server_browser *gfire_server_browser_new()
{
	gfire_server_browser *ret;
	ret = (gfire_server_browser *)g_malloc0(sizeof(gfire_server_browser));

	return ret;
}

// Serverlist requests
guint16 gfire_server_browser_proto_create_friends_favorite_serverlist_request(guint32 p_gameid)
{
	guint32 offset = XFIRE_HEADER_LEN;
	p_gameid = GUINT32_TO_LE(p_gameid);

	offset = gfire_proto_write_attr_ss("gameid", 0x02, &p_gameid, sizeof(p_gameid), offset);
	gfire_proto_write_header(offset, 0x15, 1, 0);

	return offset;
}

guint16 gfire_server_browser_proto_create_serverlist_request(guint32 p_gameid)
{
	guint32 offset = XFIRE_HEADER_LEN;
	p_gameid = GUINT32_TO_LE(p_gameid);

	offset = gfire_proto_write_attr_bs(0x21, 0x02, &p_gameid, sizeof(p_gameid), offset);
	gfire_proto_write_header(offset, 0x16, 1, 0);

	return offset;
}

// Server query functions
static void gfire_server_browser_send_packet(guint32 p_ip, guint16 p_port, const gchar p_query[])
{
	if(!p_ip || !p_port || !p_query)
		return;

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(p_ip);
	address.sin_port = htons(p_port);

	// Send query
	sendto(server_browser_socket, p_query, strlen(p_query), 0, (struct sockaddr *)&address, sizeof(address));
}

static void gfire_server_browser_proto_send_query(gfire_server_browser_server *p_server)
{
	// Determine query type & perform query
	if(!g_strcmp0(p_server->protocol, "WOLFET"))
		gfire_server_browser_send_packet(p_server->ip, p_server->port, Q3_QUERY);
	else if(!g_strcmp0(p_server->protocol, "SOURCE"))
		gfire_server_browser_send_packet(p_server->ip, p_server->port, SOURCE_QUERY);
}

static gint gfire_server_browser_proto_timeout_cb(gpointer p_unused)
{
	gint i;

	for(i = 0 ; i < 20; i++)
	{
		gfire_server_browser_server *server = g_queue_pop_head(server_browser_queue);

		if(server != NULL)
		{
			// Send server query & push to queried queue
			gfire_server_browser_proto_send_query(server);
			g_queue_push_head(server_browser_queried_queue, server);
		}
	}

	return TRUE;
}

void gfire_server_brower_proto_input_cb(gpointer p_unused, PurpleInputCondition p_condition)
{
	const gchar *protocol = gfire_game_server_query_type(server_browser->current_gameid);

	// Determine protocol & parse packet
	if(!g_strcmp0(protocol, "WOLFET"))
		gfire_server_browser_proto_parse_packet_q3();
	else if(!g_strcmp0(protocol, "SOURCE"))
		gfire_server_browser_proto_parse_packet_source();
}

gboolean gfire_server_browser_proto_init()
{
	// Init socket
	if(!server_browser_socket)
	{
		if((server_browser_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
			return FALSE;
	}

	// Set socket to non-blocking mode
	int flags;

	if((flags = fcntl(server_browser_socket, F_GETFL, 0)) < 0)
		return FALSE;

	if(fcntl(server_browser_socket, F_SETFL, flags | O_NONBLOCK) < 0)
		return FALSE;

	// Add query timeout
	server_browser_event_src = g_timeout_add(500, gfire_server_browser_proto_timeout_cb, NULL);

	// Init queues
	server_browser_queue = g_queue_new();
	server_browser_queried_queue = g_queue_new();

	// Add input handler
	server_browser_socket_handle = purple_input_add(server_browser_socket, PURPLE_INPUT_READ, (PurpleInputFunction )gfire_server_brower_proto_input_cb, NULL);

	return TRUE;
}

void gfire_server_browser_proto_close()
{
	// Remove query timeout
	if(server_browser_event_src)
		g_source_remove(server_browser_event_src);

	// Remove input handler
	if(server_browser_socket_handle)
		purple_input_remove(server_browser_socket_handle);

	// Free queues
	if(server_browser_queue)
		g_queue_free(server_browser_queue);

	if(server_browser_queried_queue)
		g_queue_free(server_browser_queried_queue);

	// Close socket
	if(server_browser_socket)
		close(server_browser_socket);
}

// Serverlist handlers
void gfire_server_browser_proto_fav_serverlist_request(guint32 p_gameid)
{
	// Set current gameid
	server_browser->current_gameid = p_gameid;

	// Get fav serverlist (local)
	GList *favorite_servers = server_browser_proto_get_favorite_servers(p_gameid);

	// Push servers to fav serverlist queue as structs
	for(; favorite_servers; favorite_servers = g_list_next(favorite_servers))
	{

		gfire_server_browser_server *server;
		server = favorite_servers->data;

		// Create struct
		server->protocol = (gchar *)gfire_game_server_query_type(p_gameid);
		server->ip = server->ip;
		server->port = server->port;
		server->parent = 1;

		// Push server struct to queue
		g_queue_push_head(server_browser_queue, server);

		// Free data and go to next server
		// g_free(favorite_servers->data);
	}
}

void gfire_server_browser_proto_serverlist(gfire_data *p_gfire, guint16 p_packet_len)
{
	if(!p_gfire)
		return;

	guint32 offset = XFIRE_HEADER_LEN;

	guint32 gameid;
	GList *ips = NULL;
	GList *ports = NULL;

	if(p_packet_len < 16)
	{
		purple_debug_error("gfire", "Packet 150 received, but too short (%d bytes)\n", p_packet_len);
		return;
	}

	offset = gfire_proto_read_attr_int32_bs(p_gfire->buff_in, &gameid, 0x21, offset);
	if(offset == -1)
		return;

	offset = gfire_proto_read_attr_list_bs(p_gfire->buff_in, &ips, 0x22, offset);
	if(offset == -1 || !ips)
		return;

	offset = gfire_proto_read_attr_list_bs(p_gfire->buff_in, &ports, 0x23, offset);
	if(offset == -1 || !ports)
	{
		if(ips)
			g_list_free(ips);

		return;
	}

	purple_debug_misc("gfire", "(serverlist): Got the serverlist for %u\n", gameid);

	// Push servers to serverlist queue as structs
	for(; ips; ips = g_list_next(ips))
	{
		// Create struct
		gfire_server_browser_server *server = gfire_server_browser_server_new();

		server->protocol = (gchar *)gfire_game_server_query_type(gameid);
		server->ip = *((guint32 *)ips->data);
		server->port = *((guint16 *)ports->data);
		server->parent = 3;

		// Push server struct to queue
		g_queue_push_head(server_browser_queue, server);

		// Free data and go to next server
		g_free(ips->data);
		g_free(ports->data);

		ports = g_list_next(ports);
	}
}

void gfire_server_browser_proto_friends_favorite_serverlist(gfire_data *p_gfire, guint16 p_packet_len)
{
	if(!p_gfire)
		return;

	// FIXME: What's the correct length in bytes?
	if(p_packet_len < 16)
	{
		purple_debug_error("gfire", "Packet 149 received, but too short (%d bytes)\n", p_packet_len);
		return;
	}

	guint32 offset = XFIRE_HEADER_LEN;

	guint32 gameid;
	GList *ips = NULL;
	GList *ports = NULL;
	GList *userids = NULL;

	offset = gfire_proto_read_attr_int32_ss(p_gfire->buff_in, &gameid, "gameid", offset);

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &ips, "gip", offset);
	if(offset == -1)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &ports, "gport", offset);
	if(offset == -1)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &userids, "friends", offset);
	if(offset == -1)
		return;

	purple_debug_misc("gfire", "(serverlist): got the friends' favoritre serverlist for %u\n", gameid);

	// Push servers to serverlist queue as structs
	for(; ips; ips = g_list_next(ips))
	{
		// Create struct
		gfire_server_browser_server *server = gfire_server_browser_server_new();

		server->protocol = (gchar *)gfire_game_server_query_type(gameid);
		server->ip = *((guint32 *)ips->data);
		server->port = *((guint16 *)ports->data);
		server->parent = 2;

		// Push server struct to queue
		g_queue_push_head(server_browser_queue, server);

		// Free data and go to next server
		g_free(ips->data);
		g_free(ports->data);

		ports = g_list_next(ports);
	}
}

void gfire_server_browser_proto_favorite_serverlist(gfire_data *p_gfire, guint16 p_packet_len)
{
	if(!p_gfire)
		return;

	if(p_packet_len < 42)
	{
		purple_debug_error("gfire", "Packet 148 received, but too short (%d bytes)\n", p_packet_len);
		return;
	}

	guint32 offset = XFIRE_HEADER_LEN;

	guint32 max_favorites;
	GList *servers_gameids = NULL;
	GList *servers_ips = NULL;
	GList *servers_ports = NULL;

	offset = gfire_proto_read_attr_int32_ss(p_gfire->buff_in, &max_favorites, "max", offset);
	if(offset == -1)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &servers_gameids, "gameid", offset);
	if(offset == -1)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &servers_ips, "gip", offset);
	if(offset == -1)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &servers_ports, "gport", offset);
	if(offset == -1)
		return;

	// Initialize server browser struct & set max favorites
	server_browser = gfire_server_browser_new();
	server_browser->max_favorites = max_favorites;

	// Fill in favorite server list
	GList *favorite_servers = NULL;

	for(; servers_ips; servers_ips = g_list_next(servers_ips))
	{
		// Server (ip & port)
		gfire_server_browser_server *server;
		server = gfire_server_browser_server_new();

		server->gameid = *((guint32 *)servers_gameids->data);
		server->ip = *((guint32 *)servers_ips->data);
		server->port = *(guint32 *)servers_ports->data & 0xFFFF;

		// Add to server list
		favorite_servers = g_list_append(favorite_servers, server);

		// Get next server
		servers_gameids = g_list_next(servers_gameids);
		servers_ports = g_list_next(servers_ports);
	}

	server_browser->favorite_servers = favorite_servers;
}

// Favorite servers handling (local)
static GList *server_browser_proto_get_favorite_servers(const guint32 p_gameid)
{
	GList *ret = NULL;

	if(!p_gameid)
		return ret;

	GList *favorite_servers_tmp = g_list_copy(server_browser->favorite_servers);

	for(; favorite_servers_tmp; favorite_servers_tmp = g_list_next(favorite_servers_tmp))
	{
		gfire_server_browser_server *server;
		server = favorite_servers_tmp->data;

		if(server->gameid == p_gameid)
			ret = g_list_append(ret, server);
	}

	return ret;
}

gboolean gfire_server_browser_can_add_fav_server()
{
	if(g_list_length(server_browser->favorite_servers) < server_browser->max_favorites)
		return TRUE;
	else
		return FALSE;
}

void gfire_server_browser_add_favorite_server_local(guint32 p_gameid, guint32 p_ip, guint16 p_port)
{
	gfire_server_browser_server *server;
	server = (gfire_server_browser_server *)g_malloc0(sizeof(gfire_server_browser_server));

	server->gameid = p_gameid;
	server->ip = p_ip;
	server->port = p_port;

	// Add favorite server to local list
	server_browser->favorite_servers = g_list_append(server_browser->favorite_servers, server);
}

void gfire_server_browser_remove_favorite_server_local(guint32 p_gameid, guint32 p_ip, guint16 p_port)
{
	GList *favorite_servers_tmp = g_list_copy(server_browser->favorite_servers);

	for(; favorite_servers_tmp; favorite_servers_tmp = g_list_next(favorite_servers_tmp))
	{
		gfire_server_browser_server *server;
		server = favorite_servers_tmp->data;

		if(server->gameid == p_gameid && server->ip == p_ip && server->port == p_port)
			server_browser->favorite_servers = g_list_remove(server_browser->favorite_servers, server);
	}
}

// Packet parsers (per protocol)
void gfire_server_browser_proto_parse_packet_q3()
{
	gchar server_response[1024];

	struct sockaddr_in server;
	socklen_t server_len;

	server_len = sizeof(server);

	int n_received = recvfrom(server_browser_socket, server_response, sizeof(server_response), 0, (struct sockaddr *)&server, &server_len);

	if(n_received <= 0)
		return;

	gchar *ip = inet_ntoa(server.sin_addr);
	gint port = ntohs(server.sin_port);

	gfire_server_browser_server_info *server_info = gfire_server_browser_server_info_new();

	// Fill raw info
	g_strchomp(server_response);
	server_info->raw_info = g_strdup(server_response);

	server_info->ip_full = g_strdup_printf("%s:%d", ip, port);

	// Split response in 3 parts (message, server information, players)
	gchar **server_response_parts = g_strsplit(server_response, "\n", 0);
	// g_free(server_response);

	if(g_strv_length(server_response_parts) < 3)
		return;

	gchar **server_response_split = g_strsplit(server_response_parts[1], "\\", 0);
	if(server_response_split)
	{
		int i;
		for (i = 0; i < g_strv_length(server_response_split); i++)
		{
			// IP & port
			gfire_game_data game;
			gfire_game_data_ip_from_str(&game, ip);

			server_info->ip = game.ip.value;
			server_info->port = port;

			// Server name
			if(!g_strcmp0("sv_hostname", server_response_split[i]))
				server_info->name = server_response_split[i+1];

			// Server map
			if(!g_strcmp0("mapname", server_response_split[i]))
				server_info->map = server_response_split[i+1];

			// Max. players
			if(!g_strcmp0("sv_maxclients", server_response_split[i]))
				server_info->max_players = atoi(server_response_split[i+1]);
		}

		// Count players
		guint32 players = g_strv_length(server_response_parts) - 2;
		server_info->players = players;

		// g_strfreev(server_response_parts);
		// g_strfreev(server_response_split);
	}

	// Add server to tree strore
	gfire_server_browser_add_server(server_info);
}

void gfire_server_browser_proto_parse_packet_source()
{
	gint n_received;
	gint index, str_len;

	gchar buffer[1024];

	struct sockaddr_in server;
	socklen_t server_len;

	server_len = sizeof(server);
	n_received = recvfrom(server_browser_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&server, &server_len);

	if(n_received <= 0)
		return;

	// FIXME: Should be replaced by inet_ntoa()
	gchar *ip_str = inet_ntoa(server.sin_addr);
	gint port = ntohs(server.sin_port);

	// Check server type
	if(buffer[4] != 0x49)
		return;

	// Directly skip version for server name
	gchar hostname[256];
	index = 6;

	if((str_len = strlen(&buffer[index])) > (sizeof(hostname) -1))
	{
		if(g_strlcpy(hostname, &buffer[index], sizeof(hostname) -1) == 0)
			return;

		buffer[index + (sizeof(hostname) - 1)] = 0;
	}
	else if(buffer[index] != 0)
	{
		if(g_strlcpy(hostname, &buffer[index], str_len + 1) == 0)
			return;
	}
	else
		hostname[0] = 0;

	index += str_len + 1;

	// Map
	gchar map[256];
	sscanf(&buffer[index], "%s", map);

	index += strlen(map) + 1;

	// Game dir
	gchar game_dir[256];
	sscanf(&buffer[index], "%s", game_dir);

	index += strlen(game_dir) + 1;

	// Game description
	gchar game_description[256];

	if((str_len = strlen(&buffer[index])) > (sizeof(game_description) -1))
	{
		if(g_strlcpy(game_description, &buffer[index], sizeof(game_description) -1) == 0)
			return;

		buffer[index + (sizeof(game_description) - 1)] = 0;
	}
	else if(buffer[index] != 0)
	{
		if(g_strlcpy(game_description, &buffer[index], str_len + 1) == 0)
			return;
	}
	else
		game_description[0] = 0;

	index += strlen(game_description) + 1;

	// AppID
	// short appid = *(short *)&buffer[index];

	// Players
	gchar num_players = buffer[index + 2];
	gchar max_players = buffer[index + 3];
	// gchar num_bots = buffer[index + 4];

	/* Other vital information (not needed yet)
	gchar dedicated = buffer[index + 5];
	gchar os = buffer[index + 6];
	gchar password = buffer[index + 7];
	gchar secure = buffer[index + 8]; */

	// Create server info struct
	gfire_server_browser_server_info *server_info = gfire_server_browser_server_info_new();
	server_info->gameid = server_browser->current_gameid;

	server_info->ip = (guint32)ip_str;
	server_info->port = port;
	server_info->ip_full = g_strdup_printf("%s:%d", ip_str, port);
	server_info->players = num_players;
	server_info->max_players = max_players;
	// server_info->ping = -1;
	server_info->map = map;
	server_info->name = hostname;

	// Add server to tree strore
	gfire_server_browser_add_server(server_info);
}

// Misc.
guint16 gfire_server_browser_proto_request_add_favorite_server(guint32 p_gameid, guint32 p_ip, guint32 p_port)
{
	guint32 offset = XFIRE_HEADER_LEN;

	p_gameid = GUINT32_TO_LE(p_gameid);
	p_ip = GUINT32_TO_LE(p_ip);
	p_port = GUINT32_TO_LE(p_port);

	offset = gfire_proto_write_attr_ss("gameid", 0x02, &p_gameid, sizeof(p_gameid), offset);
	offset = gfire_proto_write_attr_ss("gip", 0x02, &p_ip, sizeof(p_ip), offset);
	offset = gfire_proto_write_attr_ss("gport", 0x02, &p_port, sizeof(p_port), offset);
	gfire_proto_write_header(offset, 0x13, 3, 0);

	return offset;
}

guint16 gfire_server_browser_proto_request_remove_favorite_server(guint32 p_gameid, guint32 p_ip, guint32 p_port)
{
	guint32 offset = XFIRE_HEADER_LEN;

	p_gameid = GUINT32_TO_LE(p_gameid);
	p_ip = GUINT32_TO_LE(p_ip);
	p_port = GUINT32_TO_LE(p_port);

	offset = gfire_proto_write_attr_ss("gameid", 0x02, &p_gameid, sizeof(p_gameid), offset);
	offset = gfire_proto_write_attr_ss("gip", 0x02, &p_ip, sizeof(p_ip), offset);
	offset = gfire_proto_write_attr_ss("gport", 0x02, &p_port, sizeof(p_port), offset);
	gfire_proto_write_header(offset, 0x14, 3, 0);

	return offset;
}

gboolean gfire_server_browser_proto_is_favorite_server(guint32 p_ip, guint16 p_port)
{
	gboolean ret = FALSE;

	GList *favorite_servers_tmp = g_list_copy(server_browser->favorite_servers);

	for(; favorite_servers_tmp; favorite_servers_tmp = g_list_next(favorite_servers_tmp))
	{
		gfire_server_browser_server *server;
		server = favorite_servers_tmp->data;

		if(server->ip == p_ip && server->port == p_port)
		{
			ret = TRUE;
			break;
		}
	}

	return ret;
}

void gfire_server_browser_proto_remove_favorite_server(gfire_data *p_gfire, guint32 p_gameid, const gchar *p_ip, const gchar *p_port)
{
	gfire_game_data game;
	gfire_game_data_ip_from_str(&game, p_ip);

	guint16 port;
	sscanf(p_port, "%hu", &port);
	game.port = port;

	guint16 packet_len = 0;
	packet_len = gfire_server_browser_proto_request_remove_favorite_server(p_gameid, game.ip.value, port);

	if(packet_len > 0)
		gfire_send(gfire_get_connection(p_gfire), packet_len);
}

void gfire_server_browser_proto_add_favorite_server(gfire_data *p_gfire, guint32 p_gameid, const gchar *p_ip, const gchar *p_port)
{
	gfire_game_data game;
	gfire_game_data_ip_from_str(&game, p_ip);

	guint16 port;
	sscanf(p_port, "%hu", &port);
	game.port = port;

	guint16 packet_len = 0;
	packet_len = gfire_server_browser_proto_request_add_favorite_server(p_gameid, game.ip.value, port);

	if(packet_len > 0)
		gfire_send(gfire_get_connection(p_gfire), packet_len);
}

void gfire_server_browser_proto_push_fav_server(gfire_server_browser_server *p_server)
{
	g_queue_push_head(server_browser_queue, p_server);
}

static gint gfire_server_brower_proto_get_parent_cb(gconstpointer a, gconstpointer b)
{
	gfire_server_browser_server *server = (gfire_server_browser_server *)a;
	gfire_server_browser_server_info *server_info = (gfire_server_browser_server_info *)b;

	// Found matching server
	if((server->ip == server_info->ip) && (server->port == server_info->port))
		return 0;

	return -1;
}

gint gfire_server_brower_proto_get_parent(gfire_server_browser_server_info *p_server)
{
	if(!p_server)
		return -1;

	GList *server_lst = g_queue_find_custom(server_browser_queried_queue, p_server,	gfire_server_brower_proto_get_parent_cb);

	if(server_lst != NULL)
	{
		gfire_server_browser_server *server = (gfire_server_browser_server *)server_lst->data;
		return server->parent;
		// g_list_free(server_lst);
	}

	return -1;
}
#endif // HAVE_GTK
