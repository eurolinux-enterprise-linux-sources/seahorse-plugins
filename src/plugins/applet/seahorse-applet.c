/* 
 * seahorse-applet.c
 * 
 * Copyright (C) 2005 Adam Schreiber <sadam@clemson.edu>
 * Copyright (C) 1999 Dave Camp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>

#include <panel-applet.h>

#include <bonobo.h>

#include <dbus/dbus-glib-bindings.h>

#include <cryptui.h>

#include "seahorse-applet.h"
#include "seahorse-gtkstock.h"
#include "seahorse-libdialogs.h"
#include "seahorse-widget.h"
#include "seahorse-util.h"
#include "seahorse-gconf.h"
#include "seahorse-secure-memory.h"
#include "seahorse-check-button-control.h"

#define APPLET_SCHEMAS                SEAHORSE_SCHEMAS "/applet"
#define SHOW_CLIPBOARD_STATE_KEY      APPLET_SCHEMAS "/show_clipboard_state"
#define DISPLAY_CLIPBOARD_ENC_KEY     APPLET_SCHEMAS "/display_encrypted_clipboard"
#define DISPLAY_CLIPBOARD_DEC_KEY     APPLET_SCHEMAS "/display_decrypted_clipboard"
#define DISPLAY_CLIPBOARD_VER_KEY     APPLET_SCHEMAS "/display_verified_clipboard"

/* -----------------------------------------------------------------------------
 * Initialize Crypto 
 */
 
 /* Setup in init_crypt */
DBusGConnection *dbus_connection = NULL;
DBusGProxy      *dbus_key_proxy = NULL;
DBusGProxy      *dbus_crypto_proxy = NULL;
CryptUIKeyset   *dbus_keyset = NULL;

static gboolean
init_crypt ()
{
    GError *error = NULL;
    
    if (!dbus_connection) {
        dbus_connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (!dbus_connection) {
            
            return FALSE;
        }

        dbus_key_proxy = dbus_g_proxy_new_for_name (dbus_connection, "org.gnome.seahorse",
                                               "/org/gnome/seahorse/keys",
                                               "org.gnome.seahorse.KeyService");
        
        dbus_crypto_proxy = dbus_g_proxy_new_for_name (dbus_connection, "org.gnome.seahorse",
                                               "/org/gnome/seahorse/crypto",
                                               "org.gnome.seahorse.CryptoService");
        
        dbus_keyset = cryptui_keyset_new ("openpgp", TRUE);
    }
    
    return TRUE;
}
/* -----------------------------------------------------------------------------
 * ICONS AND STATE 
 */

#define ICON_CLIPBOARD_UNKNOWN      "seahorse-applet-unknown"
#define ICON_CLIPBOARD_TEXT         "seahorse-applet-text"
#define ICON_CLIPBOARD_ENCRYPTED    "seahorse-applet-encrypted"
#define ICON_CLIPBOARD_SIGNED       "seahorse-applet-signed"
#define ICON_CLIPBOARD_KEY          "seahorse-applet-key"
#define ICON_CLIPBOARD_DEFAULT      ICON_CLIPBOARD_ENCRYPTED

static const gchar *clipboard_icons[] = {
    ICON_CLIPBOARD_UNKNOWN,
    ICON_CLIPBOARD_TEXT,
    ICON_CLIPBOARD_ENCRYPTED,
    ICON_CLIPBOARD_SIGNED,
    ICON_CLIPBOARD_KEY,
    NULL
};

typedef enum {
    SEAHORSE_TEXT_TYPE_NONE,
    SEAHORSE_TEXT_TYPE_PLAIN,
    SEAHORSE_TEXT_TYPE_KEY,
    SEAHORSE_TEXT_TYPE_MESSAGE,
    SEAHORSE_TEXT_TYPE_SIGNED
} SeahorseTextType;

typedef struct _SeahorsePGPHeader {
    const gchar *header;
    const gchar *footer;
    SeahorseTextType type;
} SeahorsePGPHeader;    

static const SeahorsePGPHeader seahorse_pgp_headers[] = {
    { 
        "-----BEGIN PGP MESSAGE-----", 
        "-----END PGP MESSAGE-----", 
        SEAHORSE_TEXT_TYPE_MESSAGE 
    }, 
    {
        "-----BEGIN PGP SIGNED MESSAGE-----",
        "-----END PGP SIGNATURE-----",
        SEAHORSE_TEXT_TYPE_SIGNED
    }, 
    {
        "-----BEGIN PGP PUBLIC KEY BLOCK-----",
        "-----END PGP PUBLIC KEY BLOCK-----",
        SEAHORSE_TEXT_TYPE_KEY
    }, 
    {
        "-----BEGIN PGP PRIVATE KEY BLOCK-----",
        "-----END PGP PRIVATE KEY BLOCK-----",
        SEAHORSE_TEXT_TYPE_KEY
    }
};

/* -----------------------------------------------------------------------------
 * OBJECT DECLARATION
 */

typedef struct _SeahorseAppletPrivate {
    GtkWidget           *image;
    GtkClipboard        *board;
    GtkWidget           *menu;
    SeahorseTextType    clipboard_contents;
} SeahorseAppletPrivate;

#define SEAHORSE_APPLET_GET_PRIVATE(obj)  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SEAHORSE_TYPE_APPLET, SeahorseAppletPrivate))

G_DEFINE_TYPE (SeahorseApplet, seahorse_applet, PANEL_TYPE_APPLET);

/* -----------------------------------------------------------------------------
 * INTERNAL HELPERS
 */

SeahorseTextType    
detect_text_type (const gchar *text, gint len, const gchar **start, const gchar **end)
{
    const SeahorsePGPHeader *header;
    const gchar *pos = NULL;
    const gchar *t;
    int i;
    
    if (len == -1)
        len = strlen (text);
    
    /* Find the first of the headers */
    for (i = 0; i < (sizeof (seahorse_pgp_headers) / sizeof (seahorse_pgp_headers[0])); i++) {
        t = g_strstr_len (text, len, seahorse_pgp_headers[i].header);
        if (t != NULL) {
            if (pos == NULL || (t < pos)) {
                header = &(seahorse_pgp_headers[i]);
                pos = t;
            }
        }
    }
    
    if (pos != NULL) {
        
        if (start)
            *start = pos;
        
        /* Find the end of that block */
        t = g_strstr_len (pos, len - (pos - text), header->footer);
        if (t != NULL && end)
            *end = t + strlen(header->footer);
        else if (end)
            *end = NULL;
            
        return header->type;
    }
    
    return SEAHORSE_TEXT_TYPE_PLAIN;
}

static void 
update_icon (SeahorseApplet *sapplet)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (sapplet);
    const char *stock = NULL;
    
    if (seahorse_gconf_get_boolean (SHOW_CLIPBOARD_STATE_KEY)) {
        switch (priv->clipboard_contents) {
        case SEAHORSE_TEXT_TYPE_NONE:
            stock = ICON_CLIPBOARD_UNKNOWN;
            break;
        case SEAHORSE_TEXT_TYPE_PLAIN:
            stock = ICON_CLIPBOARD_TEXT;
            break;
        case SEAHORSE_TEXT_TYPE_KEY:
            stock = ICON_CLIPBOARD_KEY;
            break;
        case SEAHORSE_TEXT_TYPE_MESSAGE:
            stock = ICON_CLIPBOARD_ENCRYPTED;
            break;
        case SEAHORSE_TEXT_TYPE_SIGNED:
            stock = ICON_CLIPBOARD_SIGNED;
            break;
        default:
            g_assert_not_reached ();
            return;
        };
        
    } else {
        stock = ICON_CLIPBOARD_DEFAULT;
    }
    
    gtk_image_set_from_stock (GTK_IMAGE (priv->image), stock, GTK_ICON_SIZE_LARGE_TOOLBAR);
}

static void
detect_received(GtkClipboard *board, const gchar *text, SeahorseApplet *sapplet)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (sapplet);
    
    if (!text || !*text)
        priv->clipboard_contents = SEAHORSE_TEXT_TYPE_NONE;
    else
        priv->clipboard_contents = detect_text_type (text, -1, NULL, NULL);
    
    update_icon (sapplet);
}

static void 
handle_clipboard_owner_change(GtkClipboard *clipboard, GdkEvent *event, 
                              SeahorseApplet *sapplet)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (sapplet);
    priv->board = clipboard; 
    
    if (seahorse_gconf_get_boolean (SHOW_CLIPBOARD_STATE_KEY)) 
        gtk_clipboard_request_text (clipboard,
             (GtkClipboardTextReceivedFunc)detect_received, sapplet);
    else
        update_icon (sapplet);
}

/* Makes URL in About Dialog Clickable */
static void about_dialog_activate_link_cb (GtkAboutDialog *about,
                                           const gchar *url,
                                           gpointer data)
{
        GtkWidget *dialog;
	GError *error = NULL;
	
	if (!g_app_info_launch_default_for_uri (url, NULL, &error)) {
		dialog = gtk_message_dialog_new (GTK_WINDOW (about), GTK_DIALOG_MODAL, 
		                                 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
		                                 _("Could not display URL: %s"),
		                                 error && error->message ? error->message : "");
		g_signal_connect (G_OBJECT (dialog), "response",
		                  G_CALLBACK (gtk_widget_destroy), NULL);
		gtk_widget_show (dialog);
	}
}

static void
about_cb (BonoboUIComponent *uic, SeahorseApplet *sapplet, const gchar *verbname)
{                   
    static gboolean been_here = FALSE;
    
    static const gchar *authors [] = {
        "Adam Schreiber <sadam@clemson.edu>",
        "Stef Walter <stef@memberwebs.com>",
        NULL    
    };

    static const gchar *documenters[] = {
        "Adam Schreiber <sadam@clemson.edu>",
        NULL
    };
    
    static const gchar *artists[] = {
        "Stef Walter <stef@memberwebs.com>",
        NULL    
    };
    
	if (!been_here)
	{
		been_here = TRUE;
		gtk_about_dialog_set_url_hook (about_dialog_activate_link_cb, NULL, NULL);
	}
	
    gtk_show_about_dialog (NULL, 
                "name", _("seahorse-applet"),
                "version", VERSION,
                "comments", _("Use PGP/GPG to encrypt/decrypt/sign/verify/import the clipboard."),
                "copyright", "\xC2\xA9 2005, 2006 Adam Schreiber",
                "authors", authors,
                "documenters", documenters,
                "artists", artists,
                "translator-credits", _("translator-credits"),
                "logo-icon-name", "seahorse-applet",
                "website", "http://www.gnome.org/projects/seahorse",
                "website-label", _("Seahorse Project Homepage"),
                NULL);
}

static void
display_text (gchar *title, gchar *text, gboolean editable)
{
    GtkWidget *dialog, *scrolled_window, *text_view;
    GtkTextBuffer *buffer;
    GdkPixbuf *pixbuf;

    dialog = gtk_dialog_new_with_buttons (title, NULL, 0,
                                          GTK_STOCK_CLOSE, GTK_RESPONSE_NONE, NULL);

    gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 450);
    g_signal_connect_swapped (dialog, "response", 
                              G_CALLBACK (gtk_widget_destroy), dialog);

    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), scrolled_window);

    text_view = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), editable);
    gtk_text_view_set_left_margin (GTK_TEXT_VIEW (text_view), 8);
    gtk_text_view_set_right_margin (GTK_TEXT_VIEW (text_view), 8);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);
    gtk_container_add (GTK_CONTAINER (scrolled_window), text_view);
    
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
    gtk_text_buffer_set_text (buffer, text, strlen (text));

    pixbuf = gtk_widget_render_icon (dialog, 
                                     ICON_CLIPBOARD_DEFAULT, 
                                     (GtkIconSize)-1, 
                                     NULL);
                                     
    gtk_window_set_icon (GTK_WINDOW (dialog), pixbuf);
    
    g_object_unref(pixbuf);
    
    gtk_widget_show_all (dialog);
}

static void
notification_error (const char *title, const char *detail, SeahorseApplet *sapplet, GError *err)
{
	/* Never show an error for 'cancelled' */
	if (err->code == DBUS_GERROR_REMOTE_EXCEPTION && err->domain == DBUS_GERROR && 
	    strstr (dbus_g_error_get_name (err), "Cancelled"))
		return;

	seahorse_notification_display(title, detail, FALSE, NULL,
                                      GTK_WIDGET (sapplet));
}

static void
encrypt_received (GtkClipboard *board, const gchar *text, SeahorseApplet *sapplet)
{
    gchar *signer = NULL;
    gchar *enctext = NULL;
    gchar **keys = NULL;
    gboolean ret;
    GError *err = NULL;

    /* No text on clipboard */
    if (!text)
        return;
 
    /* Get the recipient list */
    if (cryptui_keyset_get_count (dbus_keyset) == 0)
        cryptui_need_to_get_keys ();
    else
        keys = cryptui_prompt_recipients (dbus_keyset, _("Choose Recipient Keys"), &signer);

    /* User may have cancelled */
    if (keys && *keys) {
        ret = dbus_g_proxy_call (dbus_crypto_proxy, "EncryptText", &err, 
                                G_TYPE_STRV, keys, 
                                G_TYPE_STRING, signer, 
                                G_TYPE_INT, 0,
                                G_TYPE_STRING, text,
                                G_TYPE_INVALID,
                                G_TYPE_STRING, &enctext,
                                G_TYPE_INVALID);
    
        if (ret) {
            /* And finish up */
            gtk_clipboard_set_text (board, enctext, strlen (enctext));
            detect_received (board, enctext, sapplet);
            
            if (seahorse_gconf_get_boolean (DISPLAY_CLIPBOARD_ENC_KEY) == TRUE)
                display_text (_("Encrypted Text"), enctext, FALSE);
        } else {
        	notification_error (_("Encryption Failed"), _("The clipboard could not be encrypted."), 
        	                    sapplet, err);
        	g_clear_error (&err);
        }
        
        g_strfreev(keys);
        g_free (signer);
        g_free (enctext);
    }
}

static void
encrypt_cb (GtkMenuItem *menuitem, SeahorseApplet *sapplet)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (sapplet);
    
    if(!init_crypt ())
        return;

    gtk_clipboard_request_text (priv->board,
            (GtkClipboardTextReceivedFunc)encrypt_received, sapplet);
}

static void
sign_received (GtkClipboard *board, const gchar *text, SeahorseApplet *sapplet)
{
    gchar *signer = NULL;
    gchar *enctext = NULL;
    gboolean ret;
    GError *err = NULL;
    
    /* No text on clipboard */
    if (!text)
        return;

    if (cryptui_keyset_get_count (dbus_keyset) == 0)
        cryptui_need_to_get_keys (dbus_keyset);
    else
        signer = cryptui_prompt_signer (dbus_keyset, _("Choose Key to Sign with"));
    
    if (signer == NULL)
        return;

    /* Perform the signing */
    ret = dbus_g_proxy_call (dbus_crypto_proxy, "SignText", &err, 
                                G_TYPE_STRING, signer, 
                                G_TYPE_INT, 0,
                                G_TYPE_STRING, text,
                                G_TYPE_INVALID,
                                G_TYPE_STRING, &enctext,
                                G_TYPE_INVALID);
     
     if (ret) {                          
        /* And finish up */
        gtk_clipboard_set_text (board, enctext, strlen (enctext));
        detect_received (board, enctext, sapplet);

        if (seahorse_gconf_get_boolean (DISPLAY_CLIPBOARD_ENC_KEY) == TRUE)
            display_text (_("Signed Text"), enctext, FALSE);
    } else {
        notification_error (_("Signing Failed"), _("The clipboard could not be Signed."), 
                            sapplet, err);
        g_clear_error (&err);
    }
    
    g_free (signer);
    g_free (enctext);
}

static void
sign_cb (GtkMenuItem *menuitem, SeahorseApplet *sapplet)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (sapplet);

    if(!init_crypt ())
        return;

    gtk_clipboard_request_text (priv->board,
            (GtkClipboardTextReceivedFunc)sign_received, sapplet);
}

/* When we try to 'decrypt' a key, this gets called */
static guint
import_keys (const gchar *text, SeahorseApplet *sapplet)
{
    gchar **keys, **k;
    gint nkeys = 0;
    gboolean ret;

    ret = dbus_g_proxy_call (dbus_key_proxy, "ImportKeys", NULL,
                             G_TYPE_STRING, "openpgp",
                             G_TYPE_STRING, text,
                             G_TYPE_INVALID,
                             G_TYPE_STRV, &keys,
                             G_TYPE_INVALID);
                             
    if (ret) {
        for (k = keys, nkeys = 0; *k; k++)
            nkeys++;
        g_strfreev (keys);
        
        if (!nkeys)
            seahorse_notification_display(_("Import Failed"),
                                      _("Keys were found but not imported."),
                                      FALSE,
                                      NULL,
                                      GTK_WIDGET(sapplet));
    }
    
    return nkeys;
}

/* Decrypt an encrypted message */
static gchar*
decrypt_text (const gchar *text, SeahorseApplet *sapplet)
{
    gchar *rawtext = NULL;
    gchar *signer = NULL;
    gboolean ret;
    GError *err = NULL;
    
    if (cryptui_keyset_get_count (dbus_keyset) == 0) {
        cryptui_need_to_get_keys (dbus_keyset);
        return NULL;
    }
    
    ret = dbus_g_proxy_call (dbus_crypto_proxy, "DecryptText", &err,
                             G_TYPE_STRING, "openpgp",
                             G_TYPE_INT, 0,
                             G_TYPE_STRING, text,
                             G_TYPE_INVALID, 
                             G_TYPE_STRING, &rawtext,
                             G_TYPE_STRING, &signer,
                             G_TYPE_INVALID);

    if (ret) {
        g_free (signer);
        
        return rawtext;
    } else {
        notification_error (_("Signing Failed"), _("The clipboard could not be Signed."), 
                            sapplet, err);
        g_clear_error (&err);
        return NULL;
    }
}

/* Verify a signed message */
static gchar*
verify_text (const gchar *text, SeahorseApplet *sapplet)
{
    gchar *rawtext = NULL;
    gchar *signer;
    gboolean ret;

    if (cryptui_keyset_get_count (dbus_keyset) == 0) {
        cryptui_need_to_get_keys (dbus_keyset);
        return NULL;
    }

    ret = dbus_g_proxy_call (dbus_crypto_proxy, "VerifyText", NULL,
                             G_TYPE_STRING, "openpgp",
                             G_TYPE_INT, 0,
                             G_TYPE_STRING, text,
                             G_TYPE_INVALID, 
                             G_TYPE_STRING, &rawtext,
                             G_TYPE_STRING, &signer,
                             G_TYPE_INVALID);
    
    if (ret) {
        /* Not interested in the signer */
        g_free (signer);
        return rawtext;
        
    } else {
        return NULL;
    }
}

static void
dvi_received (GtkClipboard *board, const gchar *text, SeahorseApplet *sapplet)
{
    SeahorseTextType type;      /* Type of the current block */
    gchar *rawtext = NULL;      /* Replacement text */
    guint keys = 0;             /* Number of keys imported */
    const gchar *start;         /* Pointer to start of the block */
    const gchar *end;           /* Pointer to end of the block */
    
    /* Try to figure out what clipboard contains */
    if (text == NULL)
		type = SEAHORSE_TEXT_TYPE_NONE;
	else 
	    type = detect_text_type (text, -1, &start, &end);
    
    if (type == SEAHORSE_TEXT_TYPE_NONE || type == SEAHORSE_TEXT_TYPE_PLAIN) {
        seahorse_notification_display(_("No PGP key or message was found on clipboard"),
                                      _("No PGP data found."), FALSE, NULL, GTK_WIDGET(sapplet));
		return;
	}
    
    switch (type) {

    /* A key, import it */
    case SEAHORSE_TEXT_TYPE_KEY:
        keys = import_keys (text, sapplet);
        break;

    /* A message decrypt it */
    case SEAHORSE_TEXT_TYPE_MESSAGE:
        rawtext = decrypt_text (text, sapplet);
        break;

    /* A message verify it */
    case SEAHORSE_TEXT_TYPE_SIGNED:
        rawtext = verify_text (text, sapplet);
        break;

    default:
        g_assert_not_reached ();
        break;
    };
    
    /* We got replacement text */
    if (rawtext) {

        gtk_clipboard_set_text (board, rawtext, strlen (rawtext));
        detect_received (board, rawtext, sapplet);
        
        if (((type == SEAHORSE_TEXT_TYPE_MESSAGE) && 
             (seahorse_gconf_get_boolean (DISPLAY_CLIPBOARD_DEC_KEY) == TRUE)) || 
            ((type == SEAHORSE_TEXT_TYPE_SIGNED) && 
             (seahorse_gconf_get_boolean (DISPLAY_CLIPBOARD_VER_KEY) == TRUE))) {
            /* TRANSLATORS: This means 'The text that was decrypted' */
            display_text (_("Decrypted Text"), rawtext, TRUE);
        }
        
        g_free (rawtext);
        rawtext = NULL;
    }
}

static void
dvi_cb (GtkMenuItem *menuitem, SeahorseApplet *sapplet)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (sapplet);

    if (!init_crypt ())
        return;
        
    gtk_clipboard_request_text (priv->board,
            (GtkClipboardTextReceivedFunc)dvi_received, sapplet);
}

void
properties_cb (BonoboUIComponent *uic, SeahorseApplet *sapplet, const char *verbname)
{
    SeahorseWidget *swidget;
    GtkWidget *widget;
    GdkPixbuf *pixbuf;
    
    swidget = seahorse_widget_new ("applet-preferences", NULL);
    
    widget = glade_xml_get_widget (swidget->xml, swidget->name);

    pixbuf = gtk_widget_render_icon (widget, 
                                     ICON_CLIPBOARD_DEFAULT, 
                                     (GtkIconSize)-1, 
                                     NULL);
                                     
    gtk_window_set_icon (GTK_WINDOW (widget), pixbuf);
    
    g_object_unref(pixbuf);
    
    /* Preferences window is already open */
    if (!swidget)
        return;
    
    widget = glade_xml_get_widget (swidget->xml, "show-clipboard-state");
    if (widget && GTK_IS_CHECK_BUTTON (widget))
        seahorse_check_button_gconf_attach (GTK_CHECK_BUTTON (widget), SHOW_CLIPBOARD_STATE_KEY);
    
    widget = glade_xml_get_widget (swidget->xml, "display-encrypted-clipboard");
    if (widget && GTK_IS_CHECK_BUTTON (widget))
        seahorse_check_button_gconf_attach (GTK_CHECK_BUTTON (widget), DISPLAY_CLIPBOARD_ENC_KEY);
    
    widget = glade_xml_get_widget (swidget->xml, "display-decrypted-clipboard");
    if (widget && GTK_IS_CHECK_BUTTON (widget))
        seahorse_check_button_gconf_attach (GTK_CHECK_BUTTON (widget), DISPLAY_CLIPBOARD_DEC_KEY);
        
    widget = glade_xml_get_widget (swidget->xml, "display-verified-clipboard");
    if (widget && GTK_IS_CHECK_BUTTON (widget))
        seahorse_check_button_gconf_attach (GTK_CHECK_BUTTON (widget), DISPLAY_CLIPBOARD_VER_KEY);    
    
    seahorse_widget_show (swidget);
}

static void
help_cb (BonoboUIComponent *uic, SeahorseApplet *sapplet, const char *verbname)
{
	gchar const *document = "ghelp:seahorse-applet";
	GtkWidget *dialog;
	GError *error = NULL;
    
	if (!g_app_info_launch_default_for_uri (document, NULL, &error)) {
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, 
		                                 GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
		                                 _("Could not display URL: %s"),
		                                 error && error->message ? error->message : "");
		g_signal_connect (G_OBJECT (dialog), "response",
		                  G_CALLBACK (gtk_widget_destroy), NULL);
		gtk_widget_show (dialog);
	}
}

static void
seahorse_popup_position_menu (GtkMenu *menu, int *x, int *y, gboolean *push_in, 
                              gpointer gdata)
{
    GtkWidget *widget;
    GtkRequisition requisition;
    gint menu_xpos;
    gint menu_ypos;

    widget = GTK_WIDGET (gdata);

    gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
    gdk_window_get_origin (widget->window, &menu_xpos, &menu_ypos);

    menu_xpos += widget->allocation.x;
    menu_ypos += widget->allocation.y;

    switch (panel_applet_get_orient (PANEL_APPLET (widget))) {
    case PANEL_APPLET_ORIENT_DOWN:
    case PANEL_APPLET_ORIENT_UP:
        if (menu_ypos > gdk_screen_get_height (gtk_widget_get_screen (widget)) / 2)
            menu_ypos -= requisition.height;
        else
            menu_ypos += widget->allocation.height;
        break;
    case PANEL_APPLET_ORIENT_RIGHT:
    case PANEL_APPLET_ORIENT_LEFT:
        if (menu_xpos > gdk_screen_get_width (gtk_widget_get_screen (widget)) / 2)
            menu_xpos -= requisition.width;
        else
            menu_xpos += widget->allocation.width;
        break;
    default:
        g_assert_not_reached ();
    }
           
    *x = menu_xpos;
    *y = menu_ypos;
    *push_in = TRUE;
}

static gboolean 
handle_button_press (GtkWidget *widget, GdkEventButton *event)
{
    GtkWidget *item;    
    SeahorseApplet *applet = SEAHORSE_APPLET (widget);
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (applet);
    
    if (event->button == 1) {
        
        if (priv->menu) {
            gtk_widget_destroy (priv->menu);
            priv->menu = NULL;
        }
        
        /* Build Menu */
        priv->menu = gtk_menu_new ();
        
        if (priv->clipboard_contents == SEAHORSE_TEXT_TYPE_PLAIN ||
            priv->clipboard_contents == SEAHORSE_TEXT_TYPE_NONE) {
            item = gtk_image_menu_item_new_with_mnemonic (_("_Encrypt Clipboard"));
            g_signal_connect (item, "activate", G_CALLBACK (encrypt_cb), applet);
            gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), item);
        }
        
        if (priv->clipboard_contents == SEAHORSE_TEXT_TYPE_PLAIN ||
            priv->clipboard_contents == SEAHORSE_TEXT_TYPE_NONE) {
            item = gtk_image_menu_item_new_with_mnemonic (_("_Sign Clipboard"));
            g_signal_connect (item, "activate", G_CALLBACK (sign_cb), applet);
            gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), item);
        }
        
        if (priv->clipboard_contents == SEAHORSE_TEXT_TYPE_MESSAGE ||
            priv->clipboard_contents == SEAHORSE_TEXT_TYPE_SIGNED) {
            item = gtk_image_menu_item_new_with_mnemonic (_("_Decrypt/Verify Clipboard"));
            g_signal_connect (item, "activate", G_CALLBACK (dvi_cb), applet);
            gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), item);
        }
        
        if (priv->clipboard_contents == SEAHORSE_TEXT_TYPE_KEY) {
            item = gtk_image_menu_item_new_with_mnemonic (_("_Import Keys from Clipboard"));
            g_signal_connect (item, "activate", G_CALLBACK (dvi_cb), applet);
            gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), item);
        }
        
        gtk_widget_show_all (priv->menu);
        
        gtk_menu_popup (GTK_MENU (priv->menu), NULL, NULL, seahorse_popup_position_menu, 
                        (gpointer)applet, event->button, event->time);
        return TRUE;
    }
    
    if (GTK_WIDGET_CLASS (seahorse_applet_parent_class)->button_press_event)
        return (* GTK_WIDGET_CLASS (seahorse_applet_parent_class)->button_press_event) (widget, event);
    
    return FALSE;
}

static void
set_atk_name_description (GtkWidget *widget, const gchar *name, const gchar *description)
{
    AtkObject *aobj = gtk_widget_get_accessible (widget);
    
    /* Check if gail is loaded */
    if (GTK_IS_ACCESSIBLE (aobj) == FALSE)
        return;

    atk_object_set_name (aobj, name);
    atk_object_set_description (aobj, description);
}

static void
gconf_notify (GConfClient *client, guint id, GConfEntry *entry, gpointer *data)
{
    update_icon (SEAHORSE_APPLET (data));
}

/* -----------------------------------------------------------------------------
 * OBJECT 
 */

static void
seahorse_applet_init (SeahorseApplet *applet)
{
    SeahorseAppletPrivate *priv;
    GdkAtom atom;
    GtkClipboard *board;
    
    priv = SEAHORSE_APPLET_GET_PRIVATE (applet);
    
    /* 
     * We initialize the context on first operation to avoid 
     * problems with slowing down gnome-panel loading at 
     * login.
     */
    
    priv->clipboard_contents = SEAHORSE_TEXT_TYPE_NONE;
    
    priv->image = gtk_image_new ();

    gtk_container_add (GTK_CONTAINER (applet), priv->image);

    gtk_widget_set_tooltip_text (GTK_WIDGET (applet), _("Encryption Applet"));

    set_atk_name_description (GTK_WIDGET (applet), _("Encryption Applet"), 
                              _("Use PGP/GPG to encrypt/decrypt/sign/verify/import the clipboard."));

    /* Setup Clipboard Handling */
    atom = gdk_atom_intern ("CLIPBOARD", FALSE);
    board = gtk_clipboard_get (atom);
    handle_clipboard_owner_change (board, NULL, applet);
    g_signal_connect (board, "owner-change",
                      G_CALLBACK (handle_clipboard_owner_change), applet);
    
    atom = gdk_atom_intern ("PRIMARY", FALSE);
    board = gtk_clipboard_get (atom);
    g_signal_connect (board, "owner-change",
                      G_CALLBACK (handle_clipboard_owner_change), applet);
                      
}

static void
seahorse_applet_change_background (PanelApplet *applet, PanelAppletBackgroundType type,
                                   GdkColor *colour, GdkPixmap *pixmap)
{
    GtkRcStyle *rc_style;
    GtkStyle *style;

    /* reset style */
    gtk_widget_set_style (GTK_WIDGET (applet), NULL);
    rc_style = gtk_rc_style_new ();
    gtk_widget_modify_style (GTK_WIDGET (applet), rc_style);
    g_object_unref (rc_style);

    switch (type){
    case PANEL_NO_BACKGROUND:
        break;
    case PANEL_COLOR_BACKGROUND:
        gtk_widget_modify_bg (GTK_WIDGET (applet), GTK_STATE_NORMAL, colour);
        break;
    case PANEL_PIXMAP_BACKGROUND:
        style = gtk_style_copy (GTK_WIDGET (applet)->style);

        if (style->bg_pixmap[GTK_STATE_NORMAL])
            g_object_unref (style->bg_pixmap[GTK_STATE_NORMAL]);

        style->bg_pixmap[GTK_STATE_NORMAL] = g_object_ref (pixmap);
        gtk_widget_set_style (GTK_WIDGET (applet), style);
        g_object_unref (style);
        break;
    }
}

static void
seahorse_applet_finalize (GObject *object)
{
    SeahorseAppletPrivate *priv = SEAHORSE_APPLET_GET_PRIVATE (object);

    if (priv) {
        if (priv->menu)
            gtk_widget_destroy (priv->menu);
        priv->menu = NULL;
    }
    
    if (G_OBJECT_CLASS (seahorse_applet_parent_class)->finalize)
        (* G_OBJECT_CLASS (seahorse_applet_parent_class)->finalize) (object);
        
    if (dbus_key_proxy)
        g_object_unref (dbus_key_proxy);
    dbus_key_proxy = NULL;
    
    if (dbus_crypto_proxy)
        g_object_unref (dbus_crypto_proxy);
    dbus_crypto_proxy = NULL;
    
    if (dbus_connection)
        dbus_g_connection_unref (dbus_connection);
    dbus_connection = NULL;
}

static void
seahorse_applet_class_init (SeahorseAppletClass *klass)
{
    GObjectClass *object_class;
    PanelAppletClass *applet_class;
    GtkWidgetClass *widget_class;

    object_class = G_OBJECT_CLASS (klass);
    applet_class = PANEL_APPLET_CLASS (klass);
    widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = seahorse_applet_finalize;
    applet_class->change_background = seahorse_applet_change_background;
    widget_class->button_press_event = handle_button_press;

    g_type_class_add_private (object_class, sizeof (SeahorseAppletPrivate));
    
}


/* -----------------------------------------------------------------------------
 * APPLET
 */

static const BonoboUIVerb seahorse_applet_menu_verbs [] = {
    BONOBO_UI_UNSAFE_VERB ("Props", properties_cb),
    BONOBO_UI_UNSAFE_VERB ("Help", help_cb),
    BONOBO_UI_UNSAFE_VERB ("About", about_cb),
    BONOBO_UI_VERB_END
};

static gboolean
seahorse_applet_fill (PanelApplet *applet)
{
    SeahorseApplet *sapplet = SEAHORSE_APPLET (applet);
    BonoboUIComponent *pcomp;

    /* Insert Icons into Stock */ 
    seahorse_gtkstock_init ();
    seahorse_gtkstock_add_icons (clipboard_icons);
    g_return_val_if_fail (PANEL_IS_APPLET (applet), FALSE);
    gtk_widget_show_all (GTK_WIDGET (applet));

    panel_applet_setup_menu_from_file (applet, UIDIR, "GNOME_SeahorseApplet.xml",
                                       NULL, seahorse_applet_menu_verbs, sapplet);
        
    pcomp = panel_applet_get_popup_component (applet);

    if (panel_applet_get_locked_down (applet))
        bonobo_ui_component_set_prop (pcomp, "/commands/Props", "hidden", "1", NULL);    

    update_icon (sapplet);
    seahorse_gconf_notify_lazy (APPLET_SCHEMAS, (GConfClientNotifyFunc)gconf_notify, 
                                sapplet, GTK_WIDGET (applet));
    
    return TRUE;
}

static gboolean
seahorse_applet_factory (PanelApplet *applet, const gchar *iid, gpointer data)
{
    gboolean retval = FALSE;

    if (!strcmp (iid, "OAFIID:GNOME_SeahorseApplet"))
        retval = seahorse_applet_fill (applet); 
   
    if (retval == FALSE)
        exit (-1);

    return retval;
}

/* 
 * We define our own main() since we have to do prior initialization.
 * This is copied from panel-applet.h
 */

int 
main (int argc, char *argv [])
{
    GOptionContext *context;
    GError *error;
    gint retval;

    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    context = g_option_context_new ("");
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    g_option_context_add_group (context, bonobo_activation_get_goption_group ());
    
    

    seahorse_secure_memory_init ();
    
    error = NULL; 
 	if (!g_option_context_parse (context, &argc, &argv, &error)) { 
     	if (error) { 
         	g_printerr ("Cannot parse arguments: %s.\n", 
         	error->message); 
         	g_error_free (error); 
     	} else 
         	g_printerr ("Cannot parse arguments.\n"); 
         	
     	g_option_context_free (context); 
     	
     	return 1; 
 	}
    
    gtk_init (&argc, &argv);
    
    if (!bonobo_init (&argc, argv)) {
        g_printerr ("Cannot initialize bonobo.\n");
        return 1;
    }
    
    retval =  panel_applet_factory_main ("OAFIID:GNOME_SeahorseApplet_Factory", 
                                      SEAHORSE_TYPE_APPLET, seahorse_applet_factory, NULL);
                                      
    g_option_context_free (context);
    
    return retval;

}
