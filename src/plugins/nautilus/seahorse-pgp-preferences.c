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

#include <config.h>

#include <stdlib.h>
#include <locale.h>
  
#include <glib/gi18n.h>

#include "seahorse-prefs.h"
#include "seahorse-gtkstock.h"

#ifdef WITH_AGENT
static gboolean show_cache = FALSE;
#endif

static const GOptionEntry options[] = {
#ifdef WITH_AGENT    
	{ "cache", 'c', 0, G_OPTION_ARG_NONE, &show_cache,
	    N_("For internal use"), NULL },
#endif 
    { NULL }
};

static void
destroyed (GtkObject *object, gpointer data)
{
	exit (0);
}

int
main (int argc, char **argv)
{
    SeahorseWidget *swidget;
    GOptionContext *octx = NULL;
    GError *error = NULL;

    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    octx = g_option_context_new ("");
    g_option_context_add_main_entries (octx, options, GETTEXT_PACKAGE);

    if (!gtk_init_with_args (&argc, &argv, _("Encryption Preferences"), (GOptionEntry *) options, GETTEXT_PACKAGE, &error)) {
        g_printerr ("seahorse-preferences: %s\n", error->message);
        g_error_free (error);
        exit (1);
    }

    /* Insert Icons into Stock */ 
    seahorse_gtkstock_init();
    
    swidget = seahorse_prefs_new (NULL);
	g_signal_connect (seahorse_widget_get_top (swidget), "destroy", 
                      G_CALLBACK (destroyed), NULL);

#ifdef WITH_AGENT	
    if (show_cache) {
        GtkWidget *tab = glade_xml_get_widget (swidget->xml, "cache-tab");
        seahorse_prefs_select_tab (swidget, tab);
    }
#endif
   
	gtk_main();
	return 0;
}
