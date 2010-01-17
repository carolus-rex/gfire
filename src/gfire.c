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

#include "gfire_proto.h"
#include "gf_network.h"
#include "gf_games.h"
#include "gf_server_detection.h"
#include "gf_ipc_server.h"
#include "gfire.h"

gfire_data *gfire_create(PurpleConnection *p_gc)
{
	if(!p_gc)
		return NULL;

	gfire_data *ret = (gfire_data*)g_malloc0(sizeof(gfire_data));
	if(!ret)
		return NULL;

	ret->gc = p_gc;

	ret->sid = (guint8*)g_malloc0(XFIRE_SID_LEN);
	if(!ret->sid)
		goto error;

	ret->buff_in = (guint8*)g_malloc0(GFIRE_BUFFIN_SIZE);
	if(!ret->buff_in)
		goto error;

	ret->process_list = gfire_process_list_new();
	if(!ret->process_list)
		goto error;

	ret->fd = -1;

	gfire_network_init();

	// Create server_ip_mutex
	ret->server_mutex = g_mutex_new();
	if(!ret->server_mutex)
		goto error;

	return ret;

	error:
	purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_create: Out of memory!\n");
	gfire_free(ret);
	return NULL;
}

void gfire_free(gfire_data *p_gfire)
{
	if(!p_gfire)
		return;

	if(p_gfire->sid) g_free(p_gfire->sid);
	if(p_gfire->server_mutex) g_mutex_free(p_gfire->server_mutex);
	if(p_gfire->buff_in) g_free(p_gfire->buff_in);

	// Free all buddies
	while(p_gfire->buddies)
	{
		gfire_buddy_free((gfire_buddy*)p_gfire->buddies->data);
		p_gfire->buddies = g_list_delete_link(p_gfire->buddies, p_gfire->buddies);
	}

	// Free all clans
	while(p_gfire->clans)
	{
		gfire_clan_free((gfire_clan*)p_gfire->clans->data);
		p_gfire->clans = g_list_delete_link(p_gfire->clans, p_gfire->clans);
	}

	// Free all chats
	while(p_gfire->chats)
	{
		gfire_chat_free((gfire_chat*)p_gfire->chats->data);
		p_gfire->chats = g_list_delete_link(p_gfire->chats, p_gfire->chats);
	}

	if(p_gfire->p2p)
		gfire_p2p_connection_close(p_gfire->p2p);

	// Unregister this Gfire session with the IPC server
	gfire_ipc_server_unregister(p_gfire);

	gfire_process_list_free(p_gfire->process_list);

	g_free(p_gfire);

	gfire_network_cleanup();
}

PurpleConnection *gfire_get_connection(const gfire_data *p_gfire)
{
	if(!p_gfire)
		return NULL;

	return p_gfire->gc;
}

const gchar *gfire_get_name(const gfire_data *p_gfire)
{
	if(!p_gfire)
		return NULL;

	return purple_account_get_username(purple_connection_get_account(gfire_get_connection(p_gfire)));
}

static void gfire_login_cb(gpointer p_data, gint p_source, const gchar *p_error_message)
{
	gfire_data *gfire = (gfire_data*)p_data;
	if(!gfire)
		return;

	if(p_source < 0)
	{
		purple_connection_error_reason(gfire_get_connection(gfire), PURPLE_CONNECTION_ERROR_NETWORK_ERROR, p_error_message);
		return;
	}

	PurpleAccount *account = purple_connection_get_account(gfire_get_connection(gfire));

	purple_debug(PURPLE_DEBUG_MISC, "gfire", "connected file descriptor = %d\n", p_source);
	if(!g_list_find(purple_connections_get_all(), gfire_get_connection(gfire)))
	{
		close(p_source);
		return;
	}

	gfire->fd = p_source;

	// Update the login progress status display
	purple_connection_update_progress(gfire_get_connection(gfire), "Login", 1, XFIRE_CONNECT_STEPS);

	gfire_network_buffout_write("UA01", 4, 0);
	gfire_send(gfire_get_connection(gfire), 4); /* open connection */

	// Send client version
	guint16 length = gfire_proto_create_client_version(purple_account_get_int(account, "version", XFIRE_PROTO_VERSION));
	if(length) gfire_send(gfire_get_connection(gfire), length);

	gfire_get_connection(gfire)->inpa = purple_input_add(gfire->fd, PURPLE_INPUT_READ, gfire_input_cb, gfire);

	gfire->clans = gfire_clan_get_existing();

	// Register this Gfire session with the IPC server
	gfire_ipc_server_register(gfire);

	// Setup P2P connection
	if(purple_account_get_bool(purple_connection_get_account(gfire_get_connection(gfire)), "p2p_option", FALSE)) // FIXME: disabled by default for now!
		gfire->p2p = gfire_p2p_connection_create();

	// Update Gfire if needed
	if (!gfire_update(gfire))
		purple_debug_error("gfire", "Unable to query latest Gfire and games list version. Website down?\n");
}

gboolean gfire_update(gfire_data *p_gfire)
{
	if (!p_gfire)
		return FALSE;

	purple_util_fetch_url(GFIRE_CURRENT_VERSION_XML_URL, TRUE, "purple-xfire", TRUE, gfire_update_version_cb, gfire_get_connection(p_gfire));
	return TRUE;
}

void gfire_login(gfire_data *p_gfire)
{
	if(!p_gfire)
		return;

	// Init threading system
	if(!g_thread_supported())
		g_thread_init(NULL);

	PurpleAccount *account = purple_connection_get_account(gfire_get_connection(p_gfire));

	purple_connection_update_progress(gfire_get_connection(p_gfire), _("Connecting"), 0, XFIRE_CONNECT_STEPS);

	if (purple_proxy_connect(NULL, account, purple_account_get_string(account, "server", XFIRE_SERVER),
				purple_account_get_int(account, "port", XFIRE_PORT),
				gfire_login_cb, p_gfire) == NULL)
	{
			purple_connection_error_reason(gfire_get_connection(p_gfire), PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Couldn't create socket."));
			return;
	}
}

void gfire_close(gfire_data *p_gfire)
{
	if(!p_gfire)
		return;

	PurpleConnection *gc = gfire_get_connection(p_gfire);

	purple_debug(PURPLE_DEBUG_MISC, "gfire", "CONNECTION: close requested.\n");

	if(gc->inpa)
	{
		purple_debug(PURPLE_DEBUG_MISC, "gfire", "CONN: removing input handler\n");
		purple_input_remove(gc->inpa);
	}

	if(p_gfire->det_source > 0)
	{
		purple_debug(PURPLE_DEBUG_MISC, "gfire", "CONN: removing game detection callback\n");
		g_source_remove(p_gfire->det_source);
	}

	if(p_gfire->server_browser_pool > 0)
	{
		purple_debug_misc("gfire", "CONN: removing server browser pool callback\n");
		g_source_remove(p_gfire->server_browser_pool);
	}

	if(p_gfire->fd >= 0)
	{
		purple_debug(PURPLE_DEBUG_MISC, "gfire", "CONN: closing source file descriptor\n");
		close(p_gfire->fd);
		p_gfire->fd = -1;
	}

	gfire_free(p_gfire);

	gc->proto_data = NULL;
}

gfire_buddy *gfire_find_buddy(gfire_data *p_gfire, const void *p_data, gfire_find_buddy_mode p_mode)
{
	gfire_buddy *b = NULL;

	if (!p_gfire || !p_data)
		return NULL;

	GList *buddy = p_gfire->buddies;
	for(; buddy; buddy = g_list_next(buddy))
	{
		switch(p_mode)
		{
			case GFFB_NAME:
				b = (gfire_buddy *)buddy->data;
				if(0 == g_ascii_strcasecmp((const gchar*)p_data, gfire_buddy_get_name(b)))
					return b;
			break;
			case GFFB_ALIAS:
				b = (gfire_buddy *)buddy->data;
				if(0 == g_ascii_strcasecmp((const gchar*)p_data, gfire_buddy_get_alias(b)))
					return b;
			break;
			case GFFB_USERID:
				b = (gfire_buddy *)buddy->data;
				if(gfire_buddy_is_by_userid(b, *(const guint32*)p_data))
					return b;
			break;
			case GFFB_SID:
				b = (gfire_buddy *)buddy->data;
				if(gfire_buddy_is_by_sid(b, (const guint8*)p_data))
					return b;
			break;
			default:
				/* mode not implemented */
				purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_find_buddy: called with invalid mode\n");
				return NULL;
			break;
		}
	}
	return NULL;
}

void gfire_add_buddy(gfire_data *p_gfire, gfire_buddy *p_buddy, PurpleGroup *p_group)
{
	if(!p_gfire || !p_buddy)
		return;

	p_buddy->gc = gfire_get_connection(p_gfire);
	p_gfire->buddies = g_list_append(p_gfire->buddies, p_buddy);
	gfire_buddy_prpl_add(p_buddy, p_group);

	purple_debug_info("gfire", "Added Buddy: Name=%s / Alias=%s / Type=%u\n",
					  gfire_buddy_get_name(p_buddy), gfire_buddy_get_alias(p_buddy), p_buddy->type);
}

void gfire_remove_buddy(gfire_data *p_gfire, gfire_buddy *p_buddy, gboolean p_fromServer, gboolean p_force)
{
	if(!p_gfire || !p_buddy)
		return;

	// Delete from server
	if(p_fromServer && gfire_buddy_is_friend(p_buddy))
	{
		guint16 packet_len = gfire_proto_create_delete_buddy(p_buddy->userid);
		gfire_send(gfire_get_connection(p_gfire), packet_len);
	}

	// Delete from list
	GList *entry = g_list_find(p_gfire->buddies, p_buddy);
	if(!entry)
		return;

	if(p_force) gfire_buddy_prpl_remove((gfire_buddy*)entry->data);
	gfire_buddy_free((gfire_buddy*)entry->data);
	p_gfire->buddies = g_list_delete_link(p_gfire->buddies, entry);
}

void gfire_authenticate(gfire_data *p_gfire, const gchar *p_salt)
{
	if(!p_gfire || !p_salt)
		return;

	PurpleAccount *account = purple_connection_get_account(gfire_get_connection(p_gfire));

	// Generate password hash
	gchar *username = g_utf8_strdown(purple_account_get_username(account), -1);
	gchar *tmp_str = g_strdup_printf("%s%sUltimateArena", username, purple_account_get_password(account));
	g_free(username);

	gchar pw_hash[41];
	hashSha1(tmp_str, pw_hash);

	g_free(tmp_str);
	tmp_str = g_strdup_printf("%s%s", pw_hash, p_salt);
	hashSha1(tmp_str, pw_hash);
	g_free(tmp_str);

	// Send the packet
	guint16 len = gfire_proto_create_auth(purple_account_get_username(account), pw_hash);
	if(len > 0) gfire_send(gfire_get_connection(p_gfire), len);

	purple_connection_update_progress(gfire_get_connection(p_gfire), _("Login sent"), 2, XFIRE_CONNECT_STEPS);
}

void gfire_set_status(gfire_data *p_gfire, const PurpleStatus *p_status)
{
	if(!p_gfire || !p_status)
		return;

	gchar *nohtmltags = purple_markup_strip_html(purple_status_get_attr_string(p_status, "message"));
	gchar *msg = purple_unescape_html(nohtmltags);
	g_free(nohtmltags);
	gchar *status = NULL;
	switch(purple_status_type_get_primitive(purple_status_get_type(p_status)))
	{
		case PURPLE_STATUS_AVAILABLE:
			if(msg)
				status = g_strdup(msg);
			else
				status = g_strdup("");
		break;
		case PURPLE_STATUS_AWAY:
			if(msg && strlen(msg) > 0)
				status = g_strdup_printf("(AFK) %s", msg);
			else
				// TRANSLATORS: Keep "(AFK)" as is!
				// Suggestion: Use Xfires original AFK message here
				status = g_strdup(_("(AFK) Away From Keyboard"));
		break;
		case PURPLE_STATUS_UNAVAILABLE:
			if(msg && strlen(msg) > 0)
				status = g_strdup_printf("(Busy) %s", msg);
			else
				// TRANSLATORS: Keep "(Busy)" as is!
				status = g_strdup(_("(Busy) I'm busy!"));
		break;
		default:
			return;
	}

	g_free(msg);

	purple_debug(PURPLE_DEBUG_INFO, "gfire", "Setting status message to \"%s\"\n", status);
	guint16 len = gfire_proto_create_status_text(status);
	if(len > 0) gfire_send(gfire_get_connection(p_gfire), len);

	g_free(status);
}

void gfire_set_current_status(gfire_data *p_gfire)
{
	if(!p_gfire)
		return;

	gfire_set_status(p_gfire, purple_account_get_active_status(purple_connection_get_account(gfire_get_connection(p_gfire))));
}

gboolean gfire_is_self(const gfire_data *p_gfire, guint32 p_userid)
{
	if(!p_gfire)
		return FALSE;

	return (p_gfire->userid == p_userid);
}

void gfire_add_clan(gfire_data *p_gfire, gfire_clan *p_clan)
{
	if(!p_gfire || !p_clan)
		return;

	p_gfire->clans = g_list_append(p_gfire->clans, p_clan);
}

void gfire_remove_clan(gfire_data *p_gfire, gfire_clan *p_clan)
{
	if(!p_gfire || !p_clan)
		return;

	// Delete from list
	GList *entry = g_list_find(p_gfire->clans, p_clan);
	if(!entry)
		return;

	gfire_clan_prpl_remove((gfire_clan*)entry->data);
	gfire_clan_free((gfire_clan*)entry->data);
	p_gfire->clans = g_list_delete_link(p_gfire->clans, entry);
}

gfire_clan *gfire_find_clan(gfire_data *p_gfire, guint32 p_clanid)
{
	if(!p_gfire)
		return NULL;

	GList *cur = p_gfire->clans;
	for(; cur; cur = g_list_next(cur))
		if(gfire_clan_is((gfire_clan*)cur->data, p_clanid))
			return (gfire_clan*)cur->data;

	return NULL;
}

static void gfire_buddy_invitation_authorize_cb(void *data)
{
	invitation_callback_args *args = (invitation_callback_args*)data;
	if(!args || !args->gfire || !args->name)
	{
		if(args)
		{
			if(args->name) g_free(args->name);
			g_free(args);
		}
	}

	purple_debug(PURPLE_DEBUG_INFO, "gfire", "Authorizing buddy invitation: %s\n", args->name);
	guint16 packet_len = gfire_proto_create_invitation_accept(args->name);
	if(packet_len > 0) gfire_send(gfire_get_connection(args->gfire), packet_len);

	g_free(args->name);
	g_free(args);
}

static void gfire_buddy_invitation_deny_cb(void *data)
{
	invitation_callback_args *args = (invitation_callback_args*)data;
	if(!args || !args->gfire || !args->name)
	{
		if(args)
		{
			if(args->name) g_free(args->name);
			g_free(args);
		}
	}

	purple_debug(PURPLE_DEBUG_INFO, "gfire", "Rejecting buddy invitation: %s\n", args->name);
	guint16 packet_len = gfire_proto_create_invitation_reject(args->name);
	if(packet_len > 0) gfire_send(gfire_get_connection(args->gfire), packet_len);

	g_free(args->name);
	g_free(args);
}

void gfire_got_invitation(gfire_data *p_gfire, const gchar *p_name, const gchar *p_alias, const gchar *p_msg)
{
	if(!p_gfire || !p_name || !p_alias || !p_msg)
		return;

	invitation_callback_args *args = g_malloc0(sizeof(invitation_callback_args));
	args->gfire = p_gfire;
	args->name = g_strdup(p_name);

	purple_account_request_authorization(purple_connection_get_account(gfire_get_connection(p_gfire)), p_name,
										 NULL, p_alias, p_msg, TRUE, gfire_buddy_invitation_authorize_cb,
										 gfire_buddy_invitation_deny_cb, args);
}

void gfire_set_userid(gfire_data *p_gfire, guint32 p_userid)
{
	if(!p_gfire || p_gfire->userid != 0)
		return;

	p_gfire->userid = p_userid;
	purple_debug(PURPLE_DEBUG_MISC, "gfire", "Our user ID: %u\n", p_gfire->userid);
}

void gfire_set_sid(gfire_data *p_gfire, guint8 *p_sid)
{
	if(!p_gfire || !p_sid)
		return;

	memcpy(p_gfire->sid, p_sid, XFIRE_SID_LEN);
}

void gfire_set_alias(gfire_data *p_gfire, const gchar* p_alias)
{
	if(!p_gfire || !p_alias)
		return;

	if(p_gfire->alias)
		g_free(p_gfire->alias);

	if(!p_alias)
		p_gfire->alias = NULL;
	else
		p_gfire->alias = g_strdup(p_alias);

	purple_connection_set_display_name(gfire_get_connection(p_gfire), p_alias);
}

void gfire_remove_buddy_from_clan(gfire_data *p_gfire, gfire_buddy *p_buddy, guint32 p_clanid)
{
	if(!p_gfire || !p_buddy)
		return;

	if(!gfire_buddy_is_clan_member_of(p_buddy, p_clanid))
		return;

	guint32 newclanid = 0;
	GList *cur = p_gfire->clans;
	for(; cur; cur = g_list_next(cur))
	{
		if(((gfire_clan*)cur->data)->id == p_clanid)
			continue;

		if(gfire_buddy_is_clan_member_of(p_buddy, ((gfire_clan*)cur->data)->id))
		{
			newclanid = ((gfire_clan*)cur->data)->id;
			break;
		}
	}

	if(!newclanid)
		gfire_remove_buddy(p_gfire, p_buddy, FALSE, TRUE);
	else
		gfire_buddy_remove_clan(p_buddy, p_clanid, newclanid);
}

void gfire_leave_clan(gfire_data *p_gfire, guint32 p_clanid)
{
	if(!p_gfire)
		return;

	gfire_clan *clan = gfire_find_clan(p_gfire, p_clanid);
	if(!clan)
		return;

	GList *cur = p_gfire->buddies;
	for(; cur; cur = g_list_next(cur))
	{
		gfire_buddy *buddy = (gfire_buddy*)cur->data;

		if(gfire_buddy_is_clan_member(buddy) && gfire_buddy_get_default_clan(buddy) == p_clanid)
			gfire_remove_buddy_from_clan(p_gfire, buddy, p_clanid);
	}

	gfire_clan_prpl_remove(clan);
}

static void gfire_get_info_parse_gamerig_cb(PurpleUtilFetchUrlData *p_url_data, gpointer p_data, const gchar *p_buf, gsize p_len, const gchar *p_error_message)
{
	get_info_callback_args *args = (get_info_callback_args*)p_data;

	if (!args || !p_buf || !p_len) {
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "User Info Gamerig XML Download: Download failed.\n");
		if(args)
		{
			purple_notify_user_info_add_section_break(args->user_info);
			purple_notify_user_info_add_pair(args->user_info, _("Error"), _("Retrieving gamerig data failed!"));
		}
	}
	else
	{
		purple_notify_user_info_add_section_break(args->user_info);
		xmlnode *gamerig = xmlnode_from_str(p_buf, p_len);
		if(!gamerig)
		{
			purple_notify_user_info_add_pair(args->user_info, _("Error"), _("Invalid gamerig data received!"));
		}
		else if(xmlnode_get_child(gamerig, "error"))
		{
			gchar *tmp = xmlnode_get_data(xmlnode_get_child(gamerig, "error"));
			purple_notify_user_info_add_pair(args->user_info, "Gaming Rig", NN(tmp));
			if(tmp) g_free(tmp);
			xmlnode_free(gamerig);
		}
		else
		{
			gchar *tmp = NULL;

			// Heading
			gchar *escaped_alias = gfire_escape_html(gfire_buddy_get_alias(args->gf_buddy));
			tmp = g_strdup_printf(_("%ss Gaming Rig:"), escaped_alias);
			g_free(escaped_alias);
			purple_notify_user_info_add_pair(args->user_info, tmp, NULL);
			g_free(tmp);

			// Manufacturer
			xmlnode *data = xmlnode_get_child(gamerig, "manufacturer");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Manufacturer"), tmp);
				g_free(tmp);
			}

			// CPU
			data = xmlnode_get_child(gamerig, "processor");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Processor"), tmp);
				g_free(tmp);
			}

			// Memory
			data = xmlnode_get_child(gamerig, "memory");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Memory"), tmp);
				g_free(tmp);
			}

			// Video Card
			data = xmlnode_get_child(gamerig, "videocard");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Video Card"), tmp);
				g_free(tmp);
			}

			// Sound Card
			data = xmlnode_get_child(gamerig, "soundcard");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Sound Card"), tmp);
				g_free(tmp);
			}

			// Mainboard
			data = xmlnode_get_child(gamerig, "mainboard");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Mainboard"), tmp);
				g_free(tmp);
			}

			// Hard Drive
			data = xmlnode_get_child(gamerig, "harddrive");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Hard Drive"), tmp);
				g_free(tmp);
			}

			// Monitor
			data = xmlnode_get_child(gamerig, "monitor");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Monitor"), tmp);
				g_free(tmp);
			}

			// Keyboard
			data = xmlnode_get_child(gamerig, "keyboard");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Keyboard"), tmp);
				g_free(tmp);
			}

			// Mouse
			data = xmlnode_get_child(gamerig, "mouse");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Mouse"), tmp);
				g_free(tmp);
			}

			// Mouse Surface
			data = xmlnode_get_child(gamerig, "mousesurface");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Mouse Surface"), tmp);
				g_free(tmp);
			}

			// Speakers
			data = xmlnode_get_child(gamerig, "speakers");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Speakers"), tmp);
				g_free(tmp);
			}

			// Computer Case
			data = xmlnode_get_child(gamerig, "computer_case");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Computer Case"), tmp);
				g_free(tmp);
			}

			// Operating System
			data = xmlnode_get_child(gamerig, "operatingsystem");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Operating System"), tmp);
				g_free(tmp);
			}

			xmlnode_free(gamerig);
		}
	}

	purple_debug(PURPLE_DEBUG_MISC, "gfire", "User Info Gamerig XML Download: Download successful.\n");

	if(args)
	{
		purple_notify_userinfo(args->gfire->gc, gfire_buddy_get_name(args->gf_buddy), args->user_info, NULL, NULL);

		purple_notify_user_info_destroy(args->user_info);
		g_free(args);
	}
}

static void gfire_get_info_parse_profile_cb(PurpleUtilFetchUrlData *p_url_data, gpointer p_data, const gchar *p_buf, gsize p_len, const gchar *p_error_message)
{
	get_info_callback_args *args = (get_info_callback_args*)p_data;

	if (!args || !p_buf || !p_len) {
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "User Info Profile XML Download: Download failed.\n");
		if(args)
		{
			purple_notify_user_info_add_section_break(args->user_info);
			purple_notify_user_info_add_pair(args->user_info, _("Error"), _("Retrieving profile data failed!"));
		}
	}
	else
	{
		purple_notify_user_info_add_section_break(args->user_info);
		xmlnode *profile = xmlnode_from_str(p_buf, p_len);
		if(!profile)
		{
			purple_notify_user_info_add_pair(args->user_info, _("Error"), _("Invalid profile data received!"));
		}
		else if(xmlnode_get_child(profile, "error"))
		{
			gchar *tmp = xmlnode_get_data(xmlnode_get_child(profile, "error"));
			purple_notify_user_info_add_pair(args->user_info, _("Profile"), NN(tmp));
			if(tmp) g_free(tmp);
			xmlnode_free(profile);
		}
		else
		{
			gchar *tmp = NULL;

			// Heading
			gchar *escaped_alias = gfire_escape_html(gfire_buddy_get_alias(args->gf_buddy));
			tmp = g_strdup_printf(_("%ss Profile:"), escaped_alias);
			g_free(escaped_alias);
			purple_notify_user_info_add_pair(args->user_info, tmp, NULL);
			g_free(tmp);

			// Real Name
			xmlnode *data = xmlnode_get_child(profile, "realname");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Real Name"), tmp);
				g_free(tmp);
			}

			// Age
			data = xmlnode_get_child(profile, "age");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Age"), tmp);
				g_free(tmp);
			}

			// Gender
			data = xmlnode_get_child(profile, "gender");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Gender"), (tmp[0] == 'm') ? _("Male") : _("Female"));
				g_free(tmp);
			}

			// Occupation
			data = xmlnode_get_child(profile, "occupation");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Occupation"), tmp);
				g_free(tmp);
			}

			// Country
			data = xmlnode_get_child(profile, "country");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Country"), tmp);
				g_free(tmp);
			}

			// Location
			data = xmlnode_get_child(profile, "location");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Location"), tmp);
				g_free(tmp);
			}

			// Gaming style
			data = xmlnode_get_child(profile, "gaming_style");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Gaming Style"), tmp);
				g_free(tmp);
			}

			// Interests
			data = xmlnode_get_child(profile, "interests");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Interests"), tmp);
				g_free(tmp);
			}

			// Friends
			data = xmlnode_get_child(profile, "friends_count");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Friends"), tmp);
				g_free(tmp);
			}

			// Join Date
			data = xmlnode_get_child(profile, "joindate");
			tmp = xmlnode_get_data(data);
			if(tmp)
			{
				purple_notify_user_info_add_pair(args->user_info, _("Join Date"), tmp);
				g_free(tmp);
			}

			xmlnode_free(profile);
		}
	}

	purple_debug(PURPLE_DEBUG_MISC, "gfire", "User Info Profile XML Download: Download successful.\n");

	if(args)
	{
		// Fetch Gamerig XML Data
		gchar *infoURL = g_strdup_printf(XFIRE_XML_INFO_URL, gfire_buddy_get_name(args->gf_buddy), "gamerig");
		purple_debug(PURPLE_DEBUG_MISC, "gfire", "User Info Gamerig XML Download: Starting download from %s.\n", infoURL);
		purple_util_fetch_url(infoURL, TRUE, "Purple-xfire", TRUE, gfire_get_info_parse_gamerig_cb, (void *)args);
		g_free(infoURL);
	}
}

typedef struct _gfire_buddy_info_args
{
	gfire_data *gfire;
	char *buddy_name;
	guint timeout_source;
} gfire_buddy_info_args;

static gboolean gfire_show_buddy_info_cb(gfire_buddy_info_args *p_args)
{
	if(!p_args)
		return FALSE;

	PurpleAccount *account;
	PurpleBuddy *buddy;
	gfire_buddy *gf_buddy;

	account = purple_connection_get_account(p_args->gfire->gc);
	buddy = purple_find_buddy(account, p_args->buddy_name);

	gf_buddy = gfire_find_buddy(p_args->gfire, p_args->buddy_name, GFFB_NAME);
	if(!gf_buddy)
	{
		g_free(p_args->buddy_name);
		g_free(p_args);
		return FALSE;
	}

	if(!gfire_buddy_got_info(gf_buddy))
		return TRUE;

	g_source_remove(p_args->timeout_source);

	PurpleNotifyUserInfo *user_info;
	PurplePresence *p = NULL;
	gchar *infoURL = NULL;
	get_info_callback_args *download_args = NULL;

	user_info = purple_notify_user_info_new();
	p = purple_buddy_get_presence(buddy);

	// Nickname
	gchar *tmp = gfire_escape_html(gfire_buddy_get_alias(gf_buddy));
	purple_notify_user_info_add_pair(user_info, _("Nickname"), tmp);
	if(tmp) g_free(tmp);

	// Status
	if (purple_presence_is_online(p) == TRUE)
	{
		gchar *status_msg = gfire_buddy_get_status_text(gf_buddy, TRUE);
		if(status_msg)
		{
			tmp = gfire_escape_html(status_msg);
			g_free(status_msg);
			purple_notify_user_info_add_pair(user_info, gfire_buddy_get_status_name(gf_buddy), tmp);
			if(tmp) g_free(tmp);
		}
		else
			purple_notify_user_info_add_pair(user_info, _("Status"), gfire_buddy_get_status_name(gf_buddy));
	}
	else
		purple_notify_user_info_add_pair(user_info, _("Status"), _("Offline"));

	// Game Info
	if(gfire_buddy_is_playing(gf_buddy))
	{
		const gfire_game_data *game_data = gfire_buddy_get_game_data(gf_buddy);

		gchar *tmp = gfire_game_name(game_data->id);
		purple_notify_user_info_add_pair(user_info, _("Game"), tmp);
		if(tmp) g_free(tmp);

		if (gfire_game_data_has_addr(game_data))
		{
			gchar *tmp = gfire_game_data_addr_str(game_data);
			purple_notify_user_info_add_pair(user_info, _("Server"), tmp);
			g_free(tmp);
		}
	}

	// VoIP Info
	if(gfire_buddy_is_talking(gf_buddy))
	{
		const gfire_game_data *voip_data = gfire_buddy_get_voip_data(gf_buddy);

		gchar *voip_name = gfire_game_name(voip_data->id);

		if(gfire_game_data_has_addr(voip_data))
		{
			gchar *tmp = gfire_game_data_addr_str(voip_data);
			purple_notify_user_info_add_pair(user_info, NN(voip_name), tmp);
			g_free(tmp);
		}
		else
			purple_notify_user_info_add_pair(user_info, NN(voip_name), _("unknown"));

		if(voip_name) g_free(voip_name);
	}

	// FoF common friends
	if(gfire_buddy_is_friend_of_friend(gf_buddy))
	{
		gchar *common_friends = gfire_buddy_get_common_buddies_str(gf_buddy);
		if(common_friends)
		{
			gchar *escaped_cf = gfire_escape_html(common_friends);
			g_free(common_friends);
			purple_notify_user_info_add_pair(user_info, _("Common Friends"), escaped_cf);
			g_free(escaped_cf);
		}
	}

	// Game Client Data
	const GList *gcd = gfire_buddy_get_game_client_data(gf_buddy);
	if(gcd)
	{
		purple_notify_user_info_add_section_break(user_info);
		purple_notify_user_info_add_pair(user_info, _("Additional game info:"), NULL);

		const GList *cur = gcd;
		while(cur)
		{
			purple_notify_user_info_add_pair(user_info, ((game_client_data*)cur->data)->key, ((game_client_data*)cur->data)->value);
			cur = g_list_next(cur);
		}
	}

	// Clans
	GList *clan_info = gfire_buddy_get_clans_info(gf_buddy);
	if(clan_info)
	{
		purple_notify_user_info_add_section_break(user_info);
		gchar *escaped_alias = gfire_escape_html(gfire_buddy_get_alias(gf_buddy));
		tmp = g_strdup_printf(_("%ss Clans:"), escaped_alias);
		g_free(escaped_alias);
		purple_notify_user_info_add_pair(user_info, tmp, NULL);
		g_free(tmp);

		GList *cur = clan_info;
		while(cur)
		{
			tmp = gfire_clan_get_name((gfire_clan*)cur->data);
			gchar *clan_name = gfire_escape_html(tmp);
			g_free(tmp);
			const gchar *clan_shortname = gfire_clan_get_short_name((gfire_clan*)cur->data);
			cur = g_list_next(cur);

			if(cur->data)
			{
				escaped_alias = gfire_escape_html((gchar*)cur->data);
				g_free(cur->data);
				tmp = g_strdup_printf("<a href=\"http://www.xfire.com/clans/%s/\">%s</a>: %s", clan_shortname, clan_name, escaped_alias);
				g_free(escaped_alias);
			}
			else
				tmp = g_strdup_printf("<a href=\"http://www.xfire.com/clans/%s/\">%s</a>", clan_shortname, clan_name);

			purple_notify_user_info_add_pair(user_info, NULL, tmp);
			g_free(tmp);
			g_free(clan_name);
			cur = g_list_next(cur);
		}

		g_list_free(clan_info);
	}

	// Fetch Profile XML data
	download_args = g_malloc0(sizeof(get_info_callback_args));
	download_args->gfire = p_args->gfire;
	download_args->user_info = user_info;
	download_args->gf_buddy = gf_buddy;

	infoURL = g_strdup_printf(XFIRE_XML_INFO_URL, gfire_buddy_get_name(gf_buddy), "profile");
	purple_debug(PURPLE_DEBUG_MISC, "gfire", "User Info Profile XML Download: Starting download from %s.\n", infoURL);
	purple_util_fetch_url(infoURL, TRUE, "Purple-xfire", TRUE, gfire_get_info_parse_profile_cb, (void *)download_args);
	g_free(infoURL);

	g_free(p_args->buddy_name);
	g_free(p_args);

	return FALSE;
}

void gfire_show_buddy_info(gfire_data *p_gfire, const gchar *p_name)
{
	if(!p_gfire || !p_name)
		return;

	PurpleAccount *account;
	PurpleBuddy *buddy;
	gfire_buddy *gf_buddy;

	account = purple_connection_get_account(p_gfire->gc);
	buddy = purple_find_buddy(account, p_name);

	gf_buddy = gfire_find_buddy(p_gfire, p_name, GFFB_NAME);
	if(!gf_buddy) return;

	gfire_buddy_request_info(gf_buddy);

	gfire_buddy_info_args *cb_args = g_malloc0(sizeof(gfire_buddy_info_args));
	cb_args->gfire = p_gfire;
	cb_args->buddy_name = g_strdup(p_name);

	cb_args->timeout_source = g_timeout_add_seconds(1, (GSourceFunc)gfire_show_buddy_info_cb, cb_args);
}

void gfire_keep_alive(gfire_data *p_gfire)
{
	if(!p_gfire)
		return;

	GTimeVal gtv;
	g_get_current_time(&gtv);

	if((gtv.tv_sec - p_gfire->last_response) > (XFIRE_KEEPALIVE_TIME + 60))
	{
		purple_connection_error_reason(gfire_get_connection(p_gfire), PURPLE_CONNECTION_ERROR_NETWORK_ERROR, _("Connection timed out"));
		return;
	}

	if((gtv.tv_sec - p_gfire->last_packet) > XFIRE_KEEPALIVE_TIME)
	{
		purple_debug(PURPLE_DEBUG_MISC, "gfire", "sending keep_alive packet (PING)\n");
		guint16 packet_len = gfire_proto_create_keep_alive();
		if(packet_len > 0) gfire_send(gfire_get_connection(p_gfire), packet_len);
	}
}

gfire_chat *gfire_find_chat(gfire_data *p_gfire, const void *p_data, gfire_find_chat_mode p_mode)
{
	if(!p_gfire || !p_data)
		return NULL;

	GList *chat = p_gfire->chats;

	for(; chat; chat = g_list_next(chat))
	{
		gfire_chat *c = (gfire_chat*)chat->data;
		switch(p_mode)
		{
		case GFFC_CID:
			if(memcmp(c->chat_id, p_data, XFIRE_CHATID_LEN) == 0)
				return c;
			break;
		case GFFC_PURPLEID:
			if(c->purple_id == *(int*)p_data)
				return c;
			break;
		}
	}

	return NULL;
}

void gfire_add_chat(gfire_data *p_gfire, gfire_chat *p_chat)
{
	if(!p_gfire || !p_chat)
		return;

	p_chat->gc = gfire_get_connection(p_gfire);
	p_chat->purple_id = p_gfire->chat;
	p_gfire->chat++;
	p_gfire->chats = g_list_append(p_gfire->chats, p_chat);
}

void gfire_leave_chat(gfire_data *p_gfire, gfire_chat *p_chat)
{
	if(!p_gfire || !p_chat)
		return;

	GList *cur = g_list_find(p_gfire->chats, p_chat);
	if(!cur)
		return;

	gfire_chat_leave(p_chat);
	gfire_chat_free(p_chat);
	p_gfire->chats = g_list_delete_link(p_gfire->chats, cur);
}

gboolean gfire_is_playing(const gfire_data *p_gfire)
{
	if(!p_gfire)
		return FALSE;

	return gfire_game_data_is_valid(&p_gfire->game_data);
}

gboolean gfire_is_talking(const gfire_data *p_gfire)
{
	if(!p_gfire)
		return FALSE;

	return gfire_game_data_is_valid(&p_gfire->voip_data);
}

void gfire_set_nick(gfire_data *p_gfire, const gchar *p_nick)
{
	if(!p_gfire || !p_nick)
		return;

	purple_debug(PURPLE_DEBUG_INFO, "gfire", "Changing server nick (alias) to \"%s\"\n", p_nick);
	guint16 packet_len = gfire_proto_create_change_alias(p_nick);
	if(packet_len > 0) gfire_send(gfire_get_connection(p_gfire), packet_len);
}

const gfire_game_data *gfire_get_game_data(gfire_data *p_gfire)
{
	if(!p_gfire)
		return NULL;

	return &p_gfire->game_data;
}

const gfire_game_data *gfire_get_voip_data(gfire_data *p_gfire)
{
	if(!p_gfire)
		return NULL;

	return &p_gfire->voip_data;
}

gboolean gfire_has_p2p(const gfire_data *p_gfire)
{
	return gfire_p2p_connection_running(p_gfire->p2p);
}

gfire_p2p_connection *gfire_get_p2p(const gfire_data *p_gfire)
{
	if(p_gfire && gfire_p2p_connection_running(p_gfire->p2p))
		return p_gfire->p2p;
	else
		return NULL;
}

/*
void gfire_check_for_left_clan_members(PurpleConnection *gc, guint32 clanid)
{
	gfire_data *gfire = NULL;
	GList *clans = NULL;
	GList *buddies = NULL;
	gfire_clan *clan = NULL;
	gfire_buddy *clan_member = NULL;
	PurpleGroup *clan_group = NULL;
	PurpleBlistNode *contact_buddy_node = NULL;
	PurpleBlistNode *buddy_node = NULL;
	PurpleBuddy *pbuddy = NULL;
	gfire_buddy *gf_buddy = NULL;

	if (!gc || !(gfire = (gfire_data *)gc->proto_data) || !clanid)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_check_for_left_clan_members: invalid purple connection or clanid!\n");
		return;
	}

	clans = gfire->clans;
	while(clans)
	{
		if(clans->data && ((gfire_clan*)clans->data)->clanid == clanid)
			break;
		clans = g_list_next(clans);
	}

	if(!clans)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_check_for_left_clan_members: clan not found!\n");
		return;
	}

	clan = clans->data;

	contact_buddy_node = purple_blist_node_get_first_child(&(clan->group->node));
	if(!contact_buddy_node)
		return;

	while(contact_buddy_node)
	{
		if(!PURPLE_BLIST_NODE_IS_BUDDY(contact_buddy_node) && !PURPLE_BLIST_NODE_IS_CONTACT(contact_buddy_node))
		{
			buddy_node = purple_blist_node_get_sibling_next(contact_buddy_node);
			continue;
		}
		else if(PURPLE_BLIST_NODE_IS_CONTACT(contact_buddy_node))
			buddy_node = purple_blist_node_get_first_child(contact_buddy_node);
		else
			buddy_node = contact_buddy_node;

		gboolean found = FALSE;
		pbuddy = (PurpleBuddy*)buddy_node;

		buddies = gfire->buddies;
		while(buddies)
		{
			if(!buddies->data)
			{
				buddies = g_list_next(buddies);
				continue;
			}

			gf_buddy = (gfire_buddy*)buddies->data;
			if(g_strcmp0(gf_buddy->name, purple_buddy_get_name(pbuddy)) == 0)
			{
				found = TRUE;
				break;
			}

			buddies = g_list_next(buddies);
		}

		if(!found)
		{
			purple_debug(PURPLE_DEBUG_INFO, "gfire", "%s seems to have left his clan, removing buddy\n", purple_buddy_get_name(pbuddy));
			purple_blist_remove_buddy(pbuddy);
		}

		contact_buddy_node = purple_blist_node_get_sibling_next(contact_buddy_node);
	}
}
*/

void gfire_playing_external_game(gfire_data *p_gfire, guint32 p_gameid)
{
	if(!p_gfire)
		return;

	gfire_game_data_reset(&p_gfire->game_data);
	if(p_gameid > 0)
	{
		p_gfire->game_data.id = p_gameid;
		p_gfire->external_game = TRUE;

		gchar *game_name = gfire_game_name(p_gameid);
		purple_debug_info("gfire", "%s is running. Telling Xfire ingame status.\n", NN(game_name));

		if(purple_account_get_bool(purple_connection_get_account(gfire_get_connection(p_gfire)), "ingamenotificationnorm", FALSE))
			purple_notify_message(NULL, PURPLE_NOTIFY_MSG_INFO, _("Ingame status"),
								  NN(game_name), _("Your status has been changed."), NULL, NULL);

		g_free(game_name);
	}
	else
		p_gfire->external_game = FALSE;

	guint16 len = gfire_proto_create_join_game(&p_gfire->game_data);
	if(len > 0) gfire_send(gfire_get_connection(p_gfire), len);
}

static void gfire_handle_game_detection(gfire_data *p_gfire, guint32 p_gameid, gboolean p_running, const gchar *p_executable)
{
	if (!p_gfire)
	{
		purple_debug_error("gfire", "Couldn't access gfire data.\n");
		return;
	}

	gchar *game_name = gfire_game_name(p_gameid);
	guint16 len = 0;

	g_mutex_lock(p_gfire->server_mutex);
	if (p_running == TRUE)
	{
		if (purple_account_get_bool(purple_connection_get_account(p_gfire->gc), "server_detection_option", FALSE))
			g_thread_create((GThreadFunc )gfire_server_detection_detect, p_gfire, TRUE, NULL);

		if (!gfire_is_playing(p_gfire))
		{
			gboolean norm = purple_account_get_bool(purple_connection_get_account(p_gfire->gc), "ingamenotificationnorm", FALSE);
			purple_debug_info("gfire", "%s is running, sending ingame status.\n", NN(game_name));

			// FIXME: This isn't working properly
			if (norm)
				purple_notify_message(NULL, PURPLE_NOTIFY_MSG_INFO, _("Ingame status"),
							  NN(game_name), _("Your status has been changed."), NULL, NULL);

			p_gfire->game_data.id = p_gameid;
			len = gfire_proto_create_join_game(&p_gfire->game_data);

			if (len > 0)
				gfire_send(gfire_get_connection(p_gfire), len);
		}
	}
	else
	{
		if (gfire_is_playing(p_gfire) && p_gfire->game_data.id == p_gameid)
		{
			purple_debug_misc("gfire", "Game not running anymore, sending out-of-game status.\n");

			gfire_game_data_reset(&p_gfire->game_data);
			len = gfire_proto_create_join_game(&p_gfire->game_data);

			if(len > 0)
				gfire_send(gfire_get_connection(p_gfire), len);
		}
	}
	g_mutex_unlock(p_gfire->server_mutex);

	if (p_gfire->server_changed && gfire_is_playing(p_gfire))
	{
		if (!gfire_game_data_has_addr(&p_gfire->game_data))
		{
			len = gfire_proto_create_join_game(&p_gfire->game_data);
			if(len > 0) gfire_send(gfire_get_connection(p_gfire), len);

			p_gfire->server_changed = FALSE;
		}
		else
		{
			len = gfire_proto_create_join_game(&p_gfire->game_data);
			if (len > 0)
				gfire_send(gfire_get_connection(p_gfire), len);

			p_gfire->server_changed = FALSE;
			gchar *addr = gfire_game_data_addr_str(&p_gfire->game_data);

			purple_debug_info("gfire", "Playing on server: %s\n", addr);
			g_free(addr);
		}
	}

	if (game_name)
		g_free(game_name);
}

gboolean gfire_detect_running_processes_cb(gfire_data *p_gfire)
{
	if (!p_gfire)
	{
		purple_debug_error("gfire", "Couldn't access gfire data.\n");
		return FALSE;
	}

	gboolean norm = purple_account_get_bool(purple_connection_get_account(gfire_get_connection(p_gfire)), "ingamedetectionnorm", TRUE);
	if (!norm)
		return TRUE;

	if (p_gfire->external_game)
		return TRUE;

	gfire_process_list_update(p_gfire->process_list);

	xmlnode *gfire_game = gfire_game_config_node_first();
	for (; gfire_game != NULL; gfire_game = gfire_game_config_node_next(gfire_game))
	{
		xmlnode *command_node = NULL;
		xmlnode *detect_node = NULL;

		command_node = xmlnode_get_child(gfire_game, "command");
		if(!command_node)
			continue;

		detect_node = xmlnode_get_child(command_node, "detect");
		if(!detect_node)
			continue;

		gchar *game_executable;
		const gchar *game_id;
		int game_id_int = 0;

		game_executable = xmlnode_get_data(detect_node);
		if(!game_executable)
			continue;

		game_id = xmlnode_get_attrib(gfire_game, "id");
		if(game_id)
			game_id_int = atoi(game_id);

		// Arguments are optional
		const gchar *game_exec_required_args = NULL;
		const gchar *game_exec_invalid_args = NULL;

		if (game_id_int)
		{
			const xmlnode *game_config_node = gfire_game_node_by_id(game_id_int);
			xmlnode *game_exec_args_node = NULL;

			if (game_config_node)
				game_exec_args_node = xmlnode_get_child(game_config_node, "arguments");

			if (game_exec_args_node)
			{
				game_exec_required_args = xmlnode_get_attrib(game_exec_args_node, "required");
				game_exec_invalid_args = xmlnode_get_attrib(game_exec_args_node, "invalid");
			}
		}
		else
			purple_debug_error("gfire", "Couldn't get game ID to obtain game arguments.\n");

		g_mutex_lock(p_gfire->server_mutex);
		gboolean process_running = gfire_process_list_contains(p_gfire->process_list, game_executable, game_exec_required_args, game_exec_invalid_args);
		g_mutex_unlock(p_gfire->server_mutex);

		gfire_handle_game_detection(p_gfire, game_id_int, process_running, game_executable);

		if (game_executable)
			g_free(game_executable);
	}

	return TRUE;
}

/**
 * Joins the game a buddy is playing.
 * This function launches the game and tells the game to connect to the corresponding server if needed.
 *
 * @param p_gfire: the purple connection
 * @param server_ip: the server ip to join (quad dotted notation)
 * @param server_port: the server port
 * @param game_id: the game ID to launch
 *
 * FIXME: Needs a lot of improvements
**/
void gfire_join_game(gfire_data *p_gfire, const gfire_game_data *p_game_data)
{
	if (!p_gfire)
	{
		purple_debug_error("gfire", "Couldn't access gfire data.\n");
		return;
	}

	gfire_game_config_info *game_config_info = NULL;
	gchar *game_launch_command;

	game_config_info = gfire_game_config_info_get(p_game_data->id);
	if (!game_config_info)
	{
		purple_debug_error("gfire", "Game config info struct not defined!\n");
		return;
	}

	game_launch_command = gfire_game_config_info_get_command(game_config_info, p_game_data);
	if (!game_launch_command)
	{
		purple_debug_error("gfire", "Couldn't generate game launch command!\n");
		return;
	}

	purple_debug_misc("gfire", "Launching game and joining server: %s\n", game_launch_command);
	g_spawn_command_line_async(game_launch_command, NULL);
}
