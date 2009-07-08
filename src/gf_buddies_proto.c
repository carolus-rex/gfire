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

#include "gf_protocol.h"
#include "gf_network.h"
#include "gf_buddies.h"
#include "gf_buddies_proto.h"

static GList *gfire_fof_data = NULL;

guint16 gfire_buddy_proto_create_advanced_info_request(guint32 p_userid)
{
	guint32 offset = XFIRE_HEADER_LEN;

	// '01'
	offset = gfire_proto_write_attr_bs(0x01, 0x02, &p_userid, sizeof(p_userid), offset);

	gfire_proto_write_header(offset, 0x25, 1, 0);
	return offset;
}

guint16 gfire_buddy_proto_create_typing_notification(const guint8 *p_sid, guint32 p_imindex, gboolean p_typing)
{
	if(!p_sid)
		return 0;

	guint32 offset = XFIRE_HEADER_LEN;

	// "sid"
	offset = gfire_proto_write_attr_ss("sid", 0x03, p_sid, XFIRE_SID_LEN, offset);

	// "peermsg"
	offset = gfire_proto_write_attr_ss("peermsg", 0x05, NULL, 3, offset);

	// "peermsg"->"msgtype"
	guint32 msgtype = GUINT32_TO_LE(3);
	offset = gfire_proto_write_attr_ss("msgtype", 0x02, &msgtype, sizeof(msgtype), offset);

	// "peermsg"->"imindex"
	p_imindex = GUINT32_TO_LE(p_imindex);
	offset = gfire_proto_write_attr_ss("imindex", 0x02, &p_imindex, sizeof(p_imindex), offset);

	// "peermsg"->"typing"
	guint32 typing = GUINT32_TO_LE(typing ? 1 : 0);
	offset = gfire_proto_write_attr_ss("typing", 0x02, &typing, sizeof(typing), offset);

	gfire_proto_write_header(offset, 0x02, 2, 0);
	return offset;
}

guint16 gfire_buddy_proto_create_send_im(const guint8 *p_sid, guint32 p_imindex, const gchar *p_msg)
{
	if(!p_sid || !p_msg)
		return 0;

	guint32 offset = XFIRE_HEADER_LEN;

	guint32	msgtype = GUINT32_TO_LE(0);
	p_imindex = GUINT32_TO_LE(p_imindex);

	offset = gfire_proto_write_attr_ss("sid", 0x03, p_sid, XFIRE_SID_LEN, offset);
	offset = gfire_proto_write_attr_ss("peermsg", 0x05, NULL, 3, offset);
	offset = gfire_proto_write_attr_ss("msgtype", 0x02, &msgtype, sizeof(msgtype), offset);
	offset = gfire_proto_write_attr_ss("imindex", 0x02, &p_imindex, sizeof(p_imindex), offset);
	offset = gfire_proto_write_attr_ss("im", 0x01, p_msg, strlen(p_msg), offset);

	gfire_proto_write_header(offset, 0x02, 2, 0);

	return offset;
}

guint16 gfire_buddy_proto_create_fof_request(GList *p_sids)
{
	if(!p_sids)
		return 0;

	guint32 offset = XFIRE_HEADER_LEN;

	offset = gfire_proto_write_attr_list_ss("sid", p_sids, 0x03, XFIRE_SID_LEN, offset);

	gfire_proto_write_header(offset, 0x05, 1, 0);

	return offset;
}

guint16 gfire_buddy_proto_create_ack(const guint8 *p_sid, guint32 p_imindex)
{
	if(!p_sid)
		return 0;

	guint32 offset = XFIRE_HEADER_LEN;

	guint32	msgtype = GUINT32_TO_LE(1);
	p_imindex = GUINT32_TO_LE(p_imindex);

	offset = gfire_proto_write_attr_ss("sid", 0x03, p_sid, XFIRE_SID_LEN, offset);
	offset = gfire_proto_write_attr_ss("peermsg", 0x05, NULL, 2, offset);
	offset = gfire_proto_write_attr_ss("msgtype", 0x02, &msgtype, sizeof(msgtype), offset);
	offset = gfire_proto_write_attr_ss("imindex", 0x02, &p_imindex, sizeof(p_imindex), offset);

	gfire_proto_write_header(offset, 0x02, 2, 0);

	return offset;
}

void gfire_buddy_proto_on_off(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset = 0;
	GList *userids = NULL;
	GList *sids = NULL;
	GList *u,*s;
	gfire_buddy *gf_buddy = NULL;

	if (p_packet_len < 16) {
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_on_off: buddies online status received but way too short?!? %d bytes\n",
			p_packet_len);
		return;
	}

	offset = XFIRE_HEADER_LEN;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &userids, "userid", offset);
	// Parsing error or empty list -> skip further parsing
	if(offset == -1 || !userids)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &sids, "sid", offset);
	if(offset == -1 || !sids)
	{
		gfire_list_clear(userids);
		return;
	}

	u = userids; s = sids;

	for(; u; u = g_list_next(u))
	{
		gf_buddy = gfire_find_buddy(p_gfire, u->data, GFFB_USERID);
		if (!gf_buddy)
		{
			purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_on_off: invalid user ID from Xfire\n");

			g_free(u->data);
			g_free(s->data);

			s = g_list_next(s);
			continue;
		}

		gfire_buddy_set_session_id(gf_buddy, (guint8*)s->data);

		purple_debug(PURPLE_DEBUG_INFO, "gfire", "%s is now %s\n", gfire_buddy_get_name(gf_buddy),
					gfire_buddy_is_online(gf_buddy) ? "online" : "offline");

		g_free(u->data);
		g_free(s->data);

		s = g_list_next(s);
	}

	g_list_free(userids);
	g_list_free(sids);
}

void gfire_buddy_proto_game_status(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset;
	gfire_buddy *gf_buddy = NULL;
	GList *sids = NULL;
	GList *gameids = NULL;
	GList *gameips = NULL;
	GList *gameports = NULL;
	GList *s, *g, *ip, *gp;

	offset = XFIRE_HEADER_LEN;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &sids, "sid", offset);
	// Parsing error or empty list -> skip further parsing
	if(offset == -1 || !sids)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &gameids, "gameid", offset);
	// Parsing error -> skip further parsing
	if (offset == -1)
	{
		gfire_list_clear(sids);
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &gameips, "gip", offset);
	// Parsing error -> skip further parsing
	if (offset == -1)
	{
		gfire_list_clear(sids);
		gfire_list_clear(gameids);
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &gameports, "gport", offset);
	// Parsing error -> skip further parsing
	if (offset == -1)
	{
		gfire_list_clear(sids);
		gfire_list_clear(gameids);
		gfire_list_clear(gameips);
		return;
	}

	g = gameids; s = sids; ip = gameips; gp = gameports;

	GList *fof_sids = NULL;
	for(; s; s = g_list_next(s))
	{
		gf_buddy = gfire_find_buddy(p_gfire, s->data, GFFB_SID);
		// Needs to be an FoF buddy
		if(!gf_buddy)
		{
			fof_sids = g_list_append(fof_sids, s->data);
			gfire_fof_data = g_list_append(gfire_fof_data, gfire_fof_game_data_create(s->data, *(guint32*)g->data, *(guint32*)ip->data, *(guint32*)gp->data & 0xFFFF));
			g_free(g->data);
			g_free(gp->data);
			g_free(ip->data);

			g = g_list_next(g);
			ip = g_list_next(ip);
			gp = g_list_next(gp);

			continue;
		}

		gfire_buddy_set_game_status(gf_buddy, *(guint32*)g->data, *(guint32*)gp->data & 0xFFFF, *(guint32*)ip->data);

		// Remove FoF as soon as he stops playing
		if(gfire_buddy_is_friend_of_friend(gf_buddy) && !gfire_buddy_is_playing(gf_buddy))
			gfire_remove_buddy(p_gfire, gf_buddy, FALSE, TRUE);

		g_free(s->data);
		g_free(g->data);
		g_free(gp->data);
		g_free(ip->data);

		g = g_list_next(g);
		ip = g_list_next(ip);
		gp = g_list_next(gp);
	}

	g_list_free(gameids);
	g_list_free(gameports);
	g_list_free(sids);
	g_list_free(gameips);

	if(g_list_length(fof_sids) > 0)
	{
		purple_debug_misc("gfire", "requesting FoF network info for %u users\n", g_list_length(fof_sids));
		guint16 len = gfire_buddy_proto_create_fof_request(fof_sids);
		if(len > 0) gfire_send(gfire_get_connection(p_gfire), len);
	}

	gfire_list_clear(fof_sids);
}

void gfire_buddy_proto_voip_status(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset;
	gfire_buddy *gf_buddy = NULL;
	GList *sids = NULL;
	GList *voipids = NULL;
	GList *voipips = NULL;
	GList *voipports = NULL;
	GList *s, *v, *ip, *vp;

	offset = XFIRE_HEADER_LEN;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &sids, "sid", offset);
	// Parsing error or empty list -> skip further parsing
	if(offset == -1 || !sids)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &voipids, "vid", offset);
	// Parsing error -> skip further parsing
	if (offset == -1)
	{
		gfire_list_clear(sids);
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &voipips, "vip", offset);
	// Parsing error -> skip further parsing
	if (offset == -1)
	{
		gfire_list_clear(sids);
		gfire_list_clear(voipids);
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &voipports, "vport", offset);
	// Parsing error -> skip further parsing
	if (offset == -1)
	{
		gfire_list_clear(sids);
		gfire_list_clear(voipids);
		gfire_list_clear(voipips);
		return;
	}

	v = voipids; s = sids; ip = voipips; vp = voipports;

	for(; s; s = g_list_next(s))
	{
		gf_buddy = gfire_find_buddy(p_gfire, s->data, GFFB_SID);
		if(!gf_buddy)
		{
			purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_voip_status: unkown session ID from Xfire\n");

			g_free(s->data);
			g_free(v->data);
			g_free(vp->data);
			g_free(ip->data);

			v = g_list_next(v);
			ip = g_list_next(ip);
			vp = g_list_next(vp);

			continue;
		}

		gfire_buddy_set_voip_status(gf_buddy, *(guint32*)v->data, *(guint32*)vp->data & 0xFFFF, *(guint32*)ip->data);

		g_free(s->data);
		g_free(v->data);
		g_free(vp->data);
		g_free(ip->data);

		v = g_list_next(v);
		ip = g_list_next(ip);
		vp = g_list_next(vp);
	}

	g_list_free(voipids);
	g_list_free(voipports);
	g_list_free(sids);
	g_list_free(voipips);
}

void gfire_buddy_proto_status_msg(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset;
	gfire_buddy *gf_buddy = NULL;
	GList *sids = NULL;
	GList *msgs = NULL;
	GList *s, *m;

	offset = XFIRE_HEADER_LEN;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &sids, "sid", offset);
	if(offset == -1)
	{
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &msgs, "msg", offset);
	if(offset == -1)
	{
		gfire_list_clear(sids);
		return;
	}

	m = msgs; s = sids;

	for(; s; s = g_list_next(s))
	{
		gf_buddy = gfire_find_buddy(p_gfire, s->data, GFFB_SID);
		if(!gf_buddy)
		{
			purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_status_msg: unkown session ID from Xfire\n");
			g_free(s->data);
			g_free(m->data);

			m = g_list_next(m);
			continue;
		}

		if(strlen((gchar*)m->data) == 0)
			gfire_buddy_set_status(gf_buddy, FALSE, NULL);
		else
			gfire_buddy_set_status(gf_buddy, TRUE, (gchar*)m->data);

		purple_debug(PURPLE_DEBUG_INFO, "gfire", "%s, is %s with msg \"%s\"\n",
					 gfire_buddy_get_name(gf_buddy), gfire_buddy_is_away(gf_buddy) ? "away" : "back",
					 gfire_buddy_get_status_text(gf_buddy) ? gfire_buddy_get_status_text(gf_buddy) : "");

		g_free(s->data);
		g_free(m->data);

		m = g_list_next(m);
	}

	g_list_free(msgs);
	g_list_free(sids);
}

void gfire_buddy_proto_alias_change(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset;
	guint32 uid = 0;
	gchar *nick = NULL;
	gfire_buddy *gf_buddy = NULL;

	offset = XFIRE_HEADER_LEN;

	// grab the userid
	offset = gfire_proto_read_attr_int32_bs(p_gfire->buff_in, &uid, 0x01, offset);
	if(offset == -1)
		return;

	// grab the new nick
	offset = gfire_proto_read_attr_string_bs(p_gfire->buff_in, &nick, 0x01, offset);
	if(offset == -1 || !nick)
		return;

	gf_buddy = gfire_find_buddy(p_gfire, (gpointer) &uid, GFFB_USERID);
	if (!gf_buddy)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_alias_change: unknown user ID from Xfire\n");
		g_free(nick);
		return;
	}

	purple_debug(PURPLE_DEBUG_INFO, "gfire", "User %s changed alias from \"%s\" to \"%s\"\n",
			gfire_buddy_get_name(gf_buddy), gfire_buddy_get_alias(gf_buddy), NN(nick));

	gfire_buddy_set_alias(gf_buddy, nick);
}

void gfire_buddy_proto_changed_avatar(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset;
	guint32 uid;
	guint32 avatarType = 0x0;
	guint32 avatarNum = 0x0;
	gfire_buddy *gf_buddy = NULL;

	offset = XFIRE_HEADER_LEN;

	// grab the userid
	offset = gfire_proto_read_attr_int32_bs(p_gfire->buff_in, &uid, 0x01, offset);
	if(offset == -1)
		return;

	// grab the avatarType
	offset = gfire_proto_read_attr_int32_bs(p_gfire->buff_in, &avatarType, 0x34, offset);
	if(offset == -1)
		return;

	// grab the avatarNum
	offset = gfire_proto_read_attr_int32_bs(p_gfire->buff_in, &avatarNum, 0x1F, offset);
	if(offset == -1)
		return;

	gf_buddy = gfire_find_buddy(p_gfire, (gpointer) &uid, GFFB_USERID);
	if(!gf_buddy)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_changed_avatar: unknown user ID from Xfire\n");
		return;
	}

	if(gfire_buddy_is_friend(gf_buddy) || gfire_buddy_is_clan_member(gf_buddy))
		gfire_buddy_download_avatar(gf_buddy, avatarType, avatarNum);
}

void gfire_buddy_proto_im(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint8 *sid, peermsg;
	guint32 msgtype,imindex = 0;
	gchar *im = NULL;
	gfire_buddy *gf_buddy = NULL;
	guint32 typing = 0;

	if(p_packet_len < 16)
	{
		purple_debug(PURPLE_DEBUG_MISC, "gfire", "ERROR: packet 133 recv'd but way too short?!? %d bytes\n",
			p_packet_len);
		return;
	}

	guint32 offset = XFIRE_HEADER_LEN;

	// get session id ("sid")
	offset = gfire_proto_read_attr_sid_ss(p_gfire->buff_in, &sid, "sid", offset);
	if(offset == -1 || !sid)
		return;

	gf_buddy = gfire_find_buddy(p_gfire, (gpointer)sid, GFFB_SID);
	if(!gf_buddy)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gfire", "gfire_buddy_proto_im: Unknown session ID for IM packet.\n");
		g_free(sid);
	}

	g_free(sid);

	// get peermsg parent attribute ("peermsg")
	offset = gfire_proto_read_attr_children_count_ss(p_gfire->buff_in, &peermsg, "peermsg", offset);
	if(offset == -1)
		return;

	// Get message type ("msgtype")
	offset = gfire_proto_read_attr_int32_ss(p_gfire->buff_in, &msgtype, "msgtype", offset);
	if(offset == -1)
		return;

	switch(msgtype)
	{
		// Instant message
		case 0:
			// IM index ("imindex")
			offset = gfire_proto_read_attr_int32_ss(p_gfire->buff_in, &imindex, "imindex", offset);
			if(offset == -1)
				return;

			// the IM itself ("im")
			offset = gfire_proto_read_attr_string_ss(p_gfire->buff_in, &im, "im", offset);
			if(offset == -1 || !im)
				return;

			gfire_buddy_got_im(gf_buddy, imindex, im);
		break;
		// ACK packet
		case 1:
			// got an ack packet from a previous IM sent
			purple_debug(PURPLE_DEBUG_MISC, "gfire", "IM ack packet received.\n");
		break;
		// P2P Info
		case 2:
			purple_debug(PURPLE_DEBUG_MISC, "gfire", "Got P2P info.\n");
			// TODO: Add handling of P2P data
		break;
		// Typing notification
		case 3:
			// IM index ("imindex")
			offset = gfire_proto_read_attr_int32_ss(p_gfire->buff_in, &imindex, "imindex", offset);
			if(offset == -1)
				return;

			// typing ("typing")
			offset = gfire_proto_read_attr_int32_ss(p_gfire->buff_in, &typing, "typing", offset);
			if(offset == -1)
				return;

			gfire_buddy_got_typing(gf_buddy, typing == 1);
		break;
		// Unknown
		default:
			purple_debug(PURPLE_DEBUG_INFO, "gfire", "unknown IM msgtype %u.\n", msgtype);
	}
}

void gfire_buddy_proto_fof_list(gfire_data *p_gfire, guint16 p_packet_len)
{
	guint32 offset;
	gfire_buddy *gf_buddy = NULL;
	GList *fofsid = NULL;
	GList *fofs = NULL;
	GList *names = NULL;
	GList *nicks = NULL;
	GList *f, *na, *n, *s;

	offset = XFIRE_HEADER_LEN;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &fofsid, "fnsid", offset);
	if(offset == -1 || !fofsid)
		return;

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &fofs, "userid", offset);
	// Parsing error or empty list -> skip further handling
	if(offset == -1 || !fofs)
	{
		gfire_list_clear(fofsid);
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &names, "name", offset);
	// Parsing error -> free other lists and skip further handling
	if(offset == -1 || !names)
	{
		gfire_list_clear(fofsid);
		gfire_list_clear(fofs);
		return;
	}

	offset = gfire_proto_read_attr_list_ss(p_gfire->buff_in, &nicks, "nick", offset);
	// Parsing error -> free other lists and skip further handling
	if(offset == -1 || !nicks)
	{
		gfire_list_clear(fofsid);
		gfire_list_clear(fofs);
		gfire_list_clear(names);
		return;
	}

	s = fofsid;
	f = fofs;
	na = names;
	n = nicks;

	for(; s; s = g_list_next(s))
	{
		// Invalid FoF
		if(*(guint32*)f->data == 0)
		{
			g_free(s->data);
			g_free(f->data);
			g_free(na->data);
			g_free(n->data);

			f = g_list_next(f);
			na = g_list_next(na);
			n = g_list_next(n);

			continue;
		}

		gf_buddy = gfire_find_buddy(p_gfire, na->data, GFFB_NAME);
		if(!gf_buddy)
		{
			gf_buddy = gfire_buddy_create(*(guint32*)f->data, (gchar*)na->data, (gchar*)n->data, GFBT_FRIEND_OF_FRIEND);
			if(gf_buddy)
			{
				gfire_add_buddy(p_gfire, gf_buddy, NULL);
				gfire_buddy_set_session_id(gf_buddy, (guint8*)s->data);

				GList *cur = gfire_fof_data;
				for(; cur; cur = g_list_next(cur))
				{
					if(memcmp(((fof_game_data*)cur->data)->sid, s->data, XFIRE_SID_LEN) == 0)
					{
						gfire_buddy_set_game_status(gf_buddy, ((fof_game_data*)cur->data)->game.id, ((fof_game_data*)cur->data)->game.port, ((fof_game_data*)cur->data)->game.ip.value);
						gfire_fof_game_data_free((fof_game_data*)cur->data);
						gfire_fof_data = g_list_delete_link(gfire_fof_data, cur);
					}
				}
			}
		}

		g_free(s->data);
		g_free(f->data);
		g_free(na->data);
		g_free(n->data);

		f = g_list_next(f);
		na = g_list_next(na);
		n = g_list_next(n);
	}

	g_list_free(fofsid);
	g_list_free(fofs);
	g_list_free(nicks);
	g_list_free(names);
}