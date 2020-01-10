/*
 * Seahorse
 *
 * Copyright (C) 2004 Stefan Walter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __SEAHORSE_AGENT_H__
#define __SEAHORSE_AGENT_H__

#include <glib.h>
#include <gconf/gconf-client.h>
#include <gconf/gconf.h>

#ifndef KL
#define KL(s)               ((sizeof(s) - 1) / sizeof(s[0]))
#endif

/* -----------------------------------------------------------------------------
 * seahorse-agent gconf keys
 */

#define AGENT_SETTINGS      "/apps/seahorse/agent"
#define SETTING_CACHE       AGENT_SETTINGS "/cache_enabled"
#define SETTING_METHOD      AGENT_SETTINGS "/cache_method"
#define SETTING_TTL         AGENT_SETTINGS "/cache_ttl"
#define SETTING_EXPIRE      AGENT_SETTINGS "/cache_expire"
#define SETTING_AUTH        AGENT_SETTINGS "/cache_authorize"
#define SETTING_DISPLAY     AGENT_SETTINGS "/cache_display"
#define METHOD_GNOME        "gnome"

/* -----------------------------------------------------------------------------
 * seahorse-agent.c
 */

/* Called from the original process before and after fork */
void seahorse_agent_prefork ();
void seahorse_agent_postfork (pid_t child);
void seahorse_agent_childsetup ();

/* Called in the new child process */
gboolean seahorse_agent_init ();
gboolean seahorse_agent_uninit (gpointer *data);
void     seahorse_agent_exit ();

/* Global options to set from the command line */
extern gboolean seahorse_agent_cshell;
extern gboolean seahorse_agent_execvars;
extern gboolean seahorse_agent_any_display;

/* -----------------------------------------------------------------------------
 * seahorse-agent-io.c
 */

struct _SeahorseAgentConn;
typedef struct _SeahorseAgentConn SeahorseAgentConn;

int seahorse_agent_io_socket ();
const gchar* seahorse_agent_io_get_socket ();
int seahorse_agent_io_init ();
void seahorse_agent_io_uninit ();
void seahorse_agent_io_reply (SeahorseAgentConn *rq, gboolean ok, const gchar *response);
void seahorse_agent_io_data (SeahorseAgentConn *cn, const gchar *data);

/* -----------------------------------------------------------------------------
 * seahorse-agent-actions.c
 */

#define SEAHORSE_AGENT_PASS_AS_DATA    0x00000001
#define SEAHORSE_AGENT_REPEAT          0x00000002

typedef struct _SeahorseAgentPassReq {
    const gchar *id;
    const gchar *errmsg;
    const gchar *prompt;
    const gchar *description;
    SeahorseAgentConn *request;
    guint32 flags;
} SeahorseAgentPassReq;

void seahorse_agent_actions_init ();
void seahorse_agent_actions_uninit ();
void seahorse_agent_actions_getpass (SeahorseAgentConn *rq, guint32 flags, gchar *id,
                                     gchar *errmsg, gchar *prompt, gchar *desc);
void seahorse_agent_actions_clrpass (SeahorseAgentConn *rq, gchar *id);
void seahorse_agent_actions_doneauth (SeahorseAgentPassReq *pr, gboolean authorized);
void seahorse_agent_actions_donepass (SeahorseAgentPassReq *pr, const gchar *pass);
void seahorse_agent_actions_nextgui ();

/* -----------------------------------------------------------------------------
 * seahorse-agent-cache.c
 */

void seahorse_agent_cache_init ();
void seahorse_agent_cache_uninit ();
const gchar *seahorse_agent_cache_get (const gchar *id);
void seahorse_agent_cache_set (const gchar *id, const gchar *pass, gboolean lock);
gboolean seahorse_agent_cache_has (const gchar *id, gboolean lock);
void seahorse_agent_cache_clear (const gchar *id);
void seahorse_agent_cache_clearall ();
guint seahorse_agent_cache_count ();
GList* seahorse_agent_cache_get_key_names ();

/* -----------------------------------------------------------------------------
 * seahorse-agent-prompt.c
 */

gboolean seahorse_agent_prompt_have ();
void seahorse_agent_prompt_pass (SeahorseAgentPassReq *pr);
void seahorse_agent_prompt_auth (SeahorseAgentPassReq *pr);
void seahorse_agent_prompt_cleanup ();

/* -----------------------------------------------------------------------------
 * seahorse-agent-status.c
 */

void seahorse_agent_status_init ();
void seahorse_agent_status_cleanup ();
void seahorse_agent_status_update ();

#endif /* __SEAHORSE_AGENT_H__ */
