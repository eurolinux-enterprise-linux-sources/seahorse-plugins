/*
 * Seahorse
 *
 * Copyright (C) 2006 Stefan Walter
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
#include "config.h"

#include <sys/types.h>
#include <sys/signal.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
  
#include <glib/gi18n.h>

#include "seahorse-gtkstock.h"
#include "seahorse-gconf.h"
#include "seahorse-agent.h"
#include "seahorse-unix-signal.h"
#include "seahorse-secure-memory.h"

static gboolean display_vars_dummy = TRUE;
static gboolean agent_no_daemonize = FALSE;
static gboolean agent_running = FALSE;
static gboolean agent_quit = FALSE;
static gchar **agent_exec_args = NULL;

static const GOptionEntry options[] = {
    { "no-daemonize", 'd', 0, G_OPTION_ARG_NONE, &agent_no_daemonize, 
        N_("Do not daemonize seahorse-agent"), NULL },

    /* TRANSLATORS: An example of a 'C type shell' is 'csh' often used by *BSD instead of bash */
    { "cshell", 'c', 0, G_OPTION_ARG_NONE, &seahorse_agent_cshell, 
        N_("Print variables in for a C type shell"), NULL },

    /* This is the default but is kept here for backward compatibility */
    { "variables", 'v', 0, G_OPTION_ARG_NONE, &display_vars_dummy, 
        N_("Display environment variables (the default)"), NULL },

    { "execute", 'x', 0, G_OPTION_ARG_NONE, &seahorse_agent_execvars, 
        N_("Execute other arguments on the command line"), NULL },
      
    /* TRANSLATORS: A 'display' is a an X display */
    { "any-display", 'A', 0, G_OPTION_ARG_NONE, &seahorse_agent_any_display, 
        N_("Allow GPG agent request from any display"), NULL },
      
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &agent_exec_args, 
        NULL, N_("command...") },

    { NULL }
};

static int
set_cloexec_flag (int fd)
{
    int oldflags;
    
    oldflags = fcntl (fd, F_GETFD, 0);
    /* If reading the flags failed, return error indication now.*/
    if (oldflags < 0)
    return oldflags;
    /* Set just the flag we want to set. */
    oldflags |= FD_CLOEXEC;

    /* Store modified flag word in the descriptor. */
    return fcntl (fd, F_SETFD, oldflags);
}

static void
daemonize (gchar **exec)
{
    /* 
     * We can't use the normal daemon call, because we have
     * special things to do in the parent after forking 
     */

    pid_t pid;
    int i, fd;

    if (agent_no_daemonize) {
        pid = getpid ();

    } else {
        switch ((pid = fork ())) {
        case -1:
            err (1, _("couldn't fork process"));
            break;

        /* The child */
        case 0:
            if (setsid () == -1)
                err (1, _("couldn't create new process group"));

            /* Close std descriptors */
            for (i = 0; i <= 2; i++)
                close (i);
            
            /* Open stdin, stdout and stderr. GPGME doesn't work without this */
            fd = open ("/dev/null", O_RDONLY, 0666);
            if (set_cloexec_flag (fd) < 0)
                g_warning ("can't set close-on-exec flag: %s", strerror (errno));
                
            fd = open ("/dev/null", O_WRONLY, 0666);
            if (set_cloexec_flag (fd) < 0)
                g_warning ("can't set close-on-exec flag: %s", strerror (errno));
                
            fd = open ("/dev/null", O_WRONLY, 0666);
            if (set_cloexec_flag (fd) < 0)
                g_warning ("can't set close-on-exec flag: %s", strerror (errno));
            
            chdir ("/tmp");
            return; /* Child process returns */
        };
    }

    /* The parent process or not daemonizing ... */

    /* Let the agent do it's thing */
    seahorse_agent_postfork (pid);
    
    if (agent_no_daemonize) {

        /* We can't overlay our process with the exec one if not daemonizing */
        if (exec && exec[0])
            g_warning ("cannot execute process when not daemonizing: %s", exec[0]);    

    } else {

        /* If we were asked to exec another program, do that here */
        if (!exec || !exec[0])
            exit (0);

        execvp (exec[0], (char**)exec);
	    g_critical ("couldn't exec %s: %s\n", exec[0], g_strerror (errno));
	    exit (1);

    }
}

static void
unix_signal (int signal)
{
    agent_quit = TRUE;
    if (agent_running)
        gtk_main_quit ();
}

static void
log_handler (const gchar *log_domain, GLogLevelFlags log_level, 
             const gchar *message, gpointer user_data)
{
    int level;

    /* Note that crit and err are the other way around in syslog */
        
    switch (G_LOG_LEVEL_MASK & log_level) {
    case G_LOG_LEVEL_ERROR:
        level = LOG_CRIT;
        break;
    case G_LOG_LEVEL_CRITICAL:
        level = LOG_ERR;
        break;
    case G_LOG_LEVEL_WARNING:
        level = LOG_WARNING;
        break;
    case G_LOG_LEVEL_MESSAGE:
        level = LOG_NOTICE;
        break;
    case G_LOG_LEVEL_INFO:
        level = LOG_INFO;
        break;
    case G_LOG_LEVEL_DEBUG:
        level = LOG_DEBUG;
        break;
    default:
        level = LOG_ERR;
        break;
    }
    
    /* Log to syslog first */
    if (log_domain)
        syslog (level, "%s: %s", log_domain, message);
    else
        syslog (level, "%s", message);
 
    /* And then to default handler for aborting and stuff like that */
    g_log_default_handler (log_domain, log_level, message, user_data); 
}

static void
prepare_logging ()
{
    GLogLevelFlags flags = G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR | 
                G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | 
                G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO;
                
    openlog ("seahorse-agent", LOG_PID, LOG_AUTH);
    
    g_log_set_handler (NULL, flags, log_handler, NULL);
    g_log_set_handler ("Glib", flags, log_handler, NULL);
    g_log_set_handler ("Gtk", flags, log_handler, NULL);
    g_log_set_handler ("Gnome", flags, log_handler, NULL);
}

int main(int argc, char* argv[])
{
    GOptionContext *octx = NULL;

    seahorse_secure_memory_init ();
    
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    
    octx = g_option_context_new ("");
    g_option_context_add_main_entries (octx, options, GETTEXT_PACKAGE);

    gtk_init_with_args (&argc, &argv, _("Encryption Key Agent (Seahorse)"), (GOptionEntry *) options, GETTEXT_PACKAGE, NULL);

    gtk_quit_add (0, (GtkFunction) seahorse_agent_uninit, NULL);
    
    seahorse_agent_prefork ();

    if (seahorse_agent_execvars && 
        (!agent_exec_args || !agent_exec_args[0]))
        errx (2, _("no command specified to execute"));

    /* 
     * All functions after this point have to print messages 
     * nicely and not just called exit() 
     */
    daemonize (seahorse_agent_execvars ? agent_exec_args : NULL);

    atexit (seahorse_agent_exit);

    g_strfreev (agent_exec_args);
    agent_exec_args = NULL;

    /* Handle some signals */
    seahorse_unix_signal_register (SIGINT, unix_signal);
    seahorse_unix_signal_register (SIGTERM, unix_signal);

    /* Force gconf to reconnect after daemonizing */
    if (!agent_no_daemonize)
        seahorse_gconf_disconnect ();    
    
    /* We log to the syslog */
    prepare_logging ();

    /* Insert Icons into Stock */
    seahorse_gtkstock_init ();
    
    if (!seahorse_agent_init ())
        agent_quit = TRUE;
    
    /* Sometimes we've already gotten a quit signal */
    if(!agent_quit) {
        agent_running = TRUE;
        gtk_main ();
        g_message ("left gtk_main\n");
    }

    /* And now clean them all up */
    seahorse_agent_uninit (NULL);

    return 0;
}
