/*
 * purple - Xfire Protocol Plugin
 *
 * Copyright (C) 2000-2001, Beat Wolf <asraniel@fryx.ch>
 * Copyright (C) 2006,      Keith Geffert <keith@penguingurus.com>
 * Copyright (C) 2008-2009	Laurent De Marez <laurentdemarez@gmail.com>
 * Copyright (C) 2009       Warren Dumortier <nwarrenfl@gmail.com>
 * Copyright (C) 2009	    Oliver Ney <oliver@dryder.de>
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

#ifndef _GF_GAMES_H
#define _GF_GAMES_H

#include "gf_base.h"

// GFIRE GAME DATA //////////////////////////////////////////////////
typedef struct _gfire_game_data
{
	guint32 id;
	guint16 port;
	union _ip_data
	{
		guint32 value;
		guint8 octets[4];
	} ip;
} gfire_game_data;

// Resetting
void gfire_game_data_reset(gfire_game_data *p_game);

// Validity checks
gboolean gfire_game_data_is_valid(const gfire_game_data *p_game);
gboolean gfire_game_data_has_addr(const gfire_game_data *p_game);

// String <-> Address conversions
void gfire_game_data_ip_from_str(gfire_game_data *p_game, const gchar *p_ipstr);
gchar *gfire_game_data_ip_str(const gfire_game_data *p_game);
gchar *gfire_game_data_port_str(const gfire_game_data *p_game);
gchar *gfire_game_data_addr_str(const gfire_game_data *p_game);

// GFIRE GAME CONFIG INFO ///////////////////////////////////////////
typedef struct _gfire_game_config_info
{
	guint32 game_id;
	gchar *game_name;
	gchar *game_prefix;
	gchar *game_launch;
	gchar *game_launch_args;
	gchar *game_connect;
} gfire_game_config_info;

// Creation and freeing
gfire_game_config_info *gfire_game_config_info_new();
void gfire_game_config_info_free(gfire_game_config_info *p_launch_info);

// Parsing
gfire_game_config_info *gfire_game_config_info_get(guint32 p_gameid);
gchar *gfire_game_config_info_get_command(gfire_game_config_info *game_config_info, const gfire_game_data *p_game_data);

// GFIRE GAME DETECTION INFO ////////////////////////////////////////
typedef struct _gfire_game_detection_info
{
	guint32 id;
	gchar *executable;
	gchar *arguments;
	gchar **excluded_ports;
	gboolean detect;
} gfire_game_detection_info;

// Creation and freeing
gfire_game_detection_info *gfire_game_detection_info_create();
void gfire_game_detection_info_free(gfire_game_detection_info *p_info);

// Parsing
gfire_game_detection_info *gfire_game_detection_info_get(guint32 p_gameid);

// GFIRE GAMES XML //////////////////////////////////////////////////
gboolean gfire_game_load_games_xml();
gboolean gfire_game_have_list();
guint32 gfire_game_get_version();
gchar *gfire_game_get_version_str();
guint32 gfire_game_id(const gchar *p_name);
gchar *gfire_game_name(guint32 p_gameid);
xmlnode *gfire_game_node_first();
xmlnode *gfire_game_node_next(xmlnode *p_node);
void gfire_update_version_cb(PurpleUtilFetchUrlData *p_url_data, gpointer p_data, const gchar *p_buf, gsize p_len, const gchar *p_error_message);
void gfire_update_games_list_cb(PurpleUtilFetchUrlData *p_url_data, gpointer p_data, const gchar *p_buf, gsize p_len, const gchar *p_error_message);
xmlnode *gfire_game_node_by_id(guint32 p_gameid);
gchar *gfire_game_server_query_type(guint32 p_gameid);

// GFIRE GAME CONFIG XML ////////////////////////////////////////////
gboolean gfire_game_load_config_xml();
gboolean gfire_game_playable(guint32 p_gameid);
xmlnode *gfire_game_config_node_first();
xmlnode *gfire_game_config_node_next(xmlnode *p_node);

// GFIRE GAME MANAGER ///////////////////////////////////////////////
#ifdef HAVE_GTK
void gfire_game_manager_show(PurplePluginAction *p_action);
#endif // HAVE_GTK

#endif // _GF_GAMES_H
