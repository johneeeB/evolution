/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Nat Friedman <nat@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>

#include <addressbook/gui/widgets/eab-config.h>
#include <mail/em-event.h>
#include <composer/e-msg-composer.h>

#include "bbdb.h"

#define d(x)

/* Plugin hooks */
gint e_plugin_lib_enable (EPlugin *ep, gint enable);
void bbdb_handle_send (EPlugin *ep, EMEventTargetComposer *target);
GtkWidget *bbdb_page_factory (EPlugin *ep, EConfigHookItemFactoryData *hook_data);

/* For internal use */
struct bbdb_stuff {
	EABConfigTargetPrefs *target;

	GtkWidget *combo_box;
	GtkWidget *gaim_combo_box;
	GtkWidget *check;
	GtkWidget *check_gaim;
};

/* Static forward declarations */
static gboolean bbdb_timeout (gpointer data);
static void bbdb_do_it (EBookClient *client, const gchar *name, const gchar *email);
static void add_email_to_contact (EContact *contact, const gchar *email);
static void enable_toggled_cb (GtkWidget *widget, gpointer data);
static void source_changed_cb (ESourceComboBox *source_combo_box, struct bbdb_stuff *stuff);
static GtkWidget *create_addressbook_combo_box (struct bbdb_stuff *stuff, gint type);
static void cleanup_cb (GObject *o, gpointer data);

/* How often check, in minutes. Read only on plugin enable. Use <= 0 to disable polling. */
static gint
get_check_interval (void)
{
	GSettings *settings;
	gint res = BBDB_BLIST_DEFAULT_CHECK_INTERVAL;

	settings = g_settings_new (CONF_SCHEMA);
	res = g_settings_get_int (settings, CONF_KEY_GAIM_CHECK_INTERVAL);

	g_object_unref (settings);

	return res * 60;
}

gint
e_plugin_lib_enable (EPlugin *ep,
                     gint enable)
{
	static guint update_source = 0;

	if (update_source) {
		g_source_remove (update_source);
		update_source = 0;
	}

	/* Start up the plugin. */
	if (enable) {
		gint interval;

		d (fprintf (stderr, "BBDB spinning up...\n"));

		g_idle_add (bbdb_timeout, ep);

		interval = get_check_interval ();
		if (interval > 0)
			update_source = g_timeout_add_seconds (interval, (GSourceFunc) bbdb_timeout, NULL);
	}

	return 0;
}

static gboolean
bbdb_timeout (gpointer data)
{
	if (bbdb_check_gaim_enabled ())
		bbdb_sync_buddy_list_check ();

	/* not a NULL for a one-time idle check, thus stop it there */
	return data == NULL;
}

typedef struct
{
	gchar *name;
	gchar *email;
} todo_struct;

static void
free_todo_struct (todo_struct *td)
{
	if (td) {
		g_free (td->name);
		g_free (td->email);
		g_free (td);
	}
}

static GSList *todo = NULL;
G_LOCK_DEFINE_STATIC (todo);

static gpointer
bbdb_do_in_thread (gpointer data)
{
	EBookClient *client = data;

	/* Open the addressbook */
	if (!client || !bbdb_open_book_client (client)) {
		G_LOCK (todo);

		g_slist_foreach (todo, (GFunc) free_todo_struct, NULL);
		g_slist_free (todo);
		todo = NULL;

		G_UNLOCK (todo);
		return NULL;
	}

	G_LOCK (todo);
	while (todo) {
		todo_struct *td = todo->data;

		todo = g_slist_remove (todo, td);

		G_UNLOCK (todo);

		if (td) {
			bbdb_do_it (client, td->name, td->email);
			free_todo_struct (td);
		}

		G_LOCK (todo);
	}
	G_UNLOCK (todo);

	g_object_unref (client);

	return NULL;
}

static void
bbdb_do_thread (const gchar *name,
                const gchar *email)
{
	todo_struct *td;

	if (!name && !email)
		return;

	td = g_new (todo_struct, 1);
	td->name = g_strdup (name);
	td->email = g_strdup (email);

	G_LOCK (todo);
	if (todo) {
		/* the list isn't empty, which means there is a thread taking
		 * care of that, thus just add it to the queue */
		todo = g_slist_append (todo, td);
	} else {
		GThread *thread;
		GError *error = NULL;
		EBookClient *client = bbdb_create_book_client (AUTOMATIC_CONTACTS_ADDRESSBOOK);

		/* list was empty, add item and create a thread */
		todo = g_slist_append (todo, td);
		thread = g_thread_try_new (NULL, bbdb_do_in_thread, client, &error);

		if (error) {
			g_warning ("%s: Creation of the thread failed with error: %s", G_STRFUNC, error->message);
			g_error_free (error);

			G_UNLOCK (todo);
			bbdb_do_in_thread (client);
			G_LOCK (todo);
		} else {
			g_thread_unref (thread);
		}
	}
	G_UNLOCK (todo);
}

static void
walk_destinations_and_free (EDestination **dests)
{
	const gchar *name, *addr;
	gint i;

	if (!dests)
		return;

	for (i = 0; dests[i] != NULL; i++) {
		if (e_destination_is_evolution_list (dests[i])) {
			const GList *members;

			for (members = e_destination_list_get_dests (dests[i]); members; members = members->next) {
				const EDestination *member = members->data;

				if (!member)
					continue;

				name = e_destination_get_name (member);
				addr = e_destination_get_email (member);

				if (name || addr)
					bbdb_do_thread (name, addr);
			}
		} else {
			name = e_destination_get_name (dests[i]);
			addr = e_destination_get_email (dests[i]);

			if (name || addr)
				bbdb_do_thread (name, addr);
		}
	}

	e_destination_freev (dests);
}

void
bbdb_handle_send (EPlugin *ep,
                  EMEventTargetComposer *target)
{
	EComposerHeaderTable *table;
	GSettings *settings;
	gboolean enable;

	settings = g_settings_new (CONF_SCHEMA);
	enable = g_settings_get_boolean (settings, CONF_KEY_ENABLE);
	g_object_unref (settings);

	if (!enable)
		return;

	table = e_msg_composer_get_header_table (target->composer);
	g_return_if_fail (table);

	/* read information from the composer, not from a generated message */
	walk_destinations_and_free (e_composer_header_table_get_destinations_to (table));
	walk_destinations_and_free (e_composer_header_table_get_destinations_cc (table));
}

static void
bbdb_do_it (EBookClient *client,
            const gchar *name,
            const gchar *email)
{
	gchar *query_string, *delim, *temp_name = NULL, *uid;
	GSList *contacts = NULL;
	gboolean status;
	EContact *contact;
	GError *error = NULL;

	g_return_if_fail (client != NULL);

	if (email == NULL || !strcmp (email, ""))
		return;

	if ((delim = strchr (email, '@')) == NULL)
		return;

	/* don't miss the entry if the mail has only e-mail id and no name */
	if (name == NULL || !strcmp (name, "")) {
		temp_name = g_strndup (email, delim - email);
		name = temp_name;
	}

	/* If any contacts exists with this email address, don't do anything */
	query_string = g_strdup_printf ("(contains \"email\" \"%s\")", email);
	status = e_book_client_get_contacts_sync (client, query_string, &contacts, NULL, NULL);
	g_free (query_string);
	if (contacts != NULL || !status) {
		g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
		g_free (temp_name);

		return;
	}

	if (g_utf8_strchr (name, -1, '\"')) {
		GString *tmp = g_string_new (name);
		gchar *p;

		while (p = g_utf8_strchr (tmp->str, tmp->len, '\"'), p)
			tmp = g_string_erase (tmp, p - tmp->str, 1);

		g_free (temp_name);
		temp_name = g_string_free (tmp, FALSE);
		name = temp_name;
	}

	contacts = NULL;
	/* If a contact exists with this name, add the email address to it. */
	query_string = g_strdup_printf ("(is \"full_name\" \"%s\")", name);
	status = e_book_client_get_contacts_sync (client, query_string, &contacts, NULL, NULL);
	g_free (query_string);
	if (contacts != NULL || !status) {
		/* FIXME: If there's more than one contact with this
		 * name, just give up; we're not smart enough for
		 * this. */
		if (!status || contacts->next != NULL) {
			g_slist_free_full (
				contacts,
				(GDestroyNotify) g_object_unref);
			g_free (temp_name);
			return;
		}

		contact = (EContact *) contacts->data;
		add_email_to_contact (contact, email);
		if (!e_book_client_modify_contact_sync (client, contact, NULL, &error)) {
			g_warning ("bbdb: Could not modify contact: %s\n", error->message);
			g_error_free (error);
		}

		g_slist_free_full (contacts, (GDestroyNotify) g_object_unref);
		g_free (temp_name);
		return;
	}

	/* Otherwise, create a new contact. */
	contact = e_contact_new ();
	e_contact_set (contact, E_CONTACT_FULL_NAME, (gpointer) name);
	add_email_to_contact (contact, email);
	g_free (temp_name);

	uid = NULL;
	if (!e_book_client_add_contact_sync (client, contact, &uid, NULL, &error)) {
		g_warning ("bbdb: Failed to add new contact: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (contact);
	g_free (uid);
}

EBookClient *
bbdb_create_book_client (gint type)
{
	EShell *shell;
	ESource *source = NULL;
	ESourceRegistry *registry;
	EBookClient *client = NULL;
	GSettings *settings;
	gboolean enable = TRUE;
	gchar *uid;
	GError *error = NULL;

	settings = g_settings_new (CONF_SCHEMA);

	/* Check to see if we're supposed to be running */
	if (type == AUTOMATIC_CONTACTS_ADDRESSBOOK)
		enable = g_settings_get_boolean (settings, CONF_KEY_ENABLE);
	if (!enable) {
		g_object_unref (settings);
		return NULL;
	}

	/* Open the appropriate addresbook. */
	if (type == GAIM_ADDRESSBOOK)
		uid = g_settings_get_string (
			settings, CONF_KEY_WHICH_ADDRESSBOOK_GAIM);
	else
		uid = g_settings_get_string (
			settings, CONF_KEY_WHICH_ADDRESSBOOK);
	g_object_unref (settings);

	shell = e_shell_get_default ();
	registry = e_shell_get_registry (shell);

	if (uid != NULL) {
		source = e_source_registry_ref_source (registry, uid);
		g_free (uid);
	}

	if (source == NULL)
		source = e_source_registry_ref_builtin_address_book (registry);

	client = e_book_client_new (source, &error);
	if (client == NULL) {
		g_warning (
			"bbdb: Failed to get addressbook: %s\n",
			error->message);
		g_error_free (error);
	}

	g_object_unref (source);

	return client;
}

gboolean
bbdb_open_book_client (EBookClient *client)
{
	GError *error = NULL;

	if (!client)
		return FALSE;

	e_client_open_sync (E_CLIENT (client), FALSE, NULL, &error);

	if (error != NULL) {
		g_warning (
			"bbdb: failed to open addressbook: %s",
			error->message);
		g_object_unref (client);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

gboolean
bbdb_check_gaim_enabled (void)
{
	GSettings *settings;
	gboolean   gaim_enabled;

	settings = g_settings_new (CONF_SCHEMA);
	gaim_enabled = g_settings_get_boolean (settings, CONF_KEY_ENABLE_GAIM);

	g_object_unref (settings);

	return gaim_enabled;
}

static void
add_email_to_contact (EContact *contact,
                      const gchar *email)
{
	GList *emails;

	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	emails = g_list_append (emails, (gpointer) email);
	e_contact_set (contact, E_CONTACT_EMAIL, (gpointer) emails);
}

/* Code to implement the configuration user interface follows */

static void
enable_toggled_cb (GtkWidget *widget,
                   gpointer data)
{
	struct bbdb_stuff *stuff = (struct bbdb_stuff *) data;
	gboolean active;
	ESource *selected_source;
	gchar *addressbook;
	GSettings *settings = g_settings_new (CONF_SCHEMA);

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	/* Save the new setting to GSettings */
	g_settings_set_boolean (settings, CONF_KEY_ENABLE, active);

	gtk_widget_set_sensitive (stuff->combo_box, active);

	addressbook = g_settings_get_string (settings, CONF_KEY_WHICH_ADDRESSBOOK);

	if (active && !addressbook) {
		selected_source = e_source_combo_box_ref_active (
			E_SOURCE_COMBO_BOX (stuff->combo_box));
		if (selected_source != NULL) {
			g_settings_set_string (
				settings, CONF_KEY_WHICH_ADDRESSBOOK,
				e_source_get_uid (selected_source));
			g_object_unref (selected_source);
		} else {
			g_settings_set_string (
				settings, CONF_KEY_WHICH_ADDRESSBOOK, "");
		}
	}

	g_free (addressbook);
	g_object_unref (settings);
}

static void
enable_gaim_toggled_cb (GtkWidget *widget,
                        gpointer data)
{
	struct bbdb_stuff *stuff = (struct bbdb_stuff *) data;
	gboolean active;
	ESource *selected_source;
	gchar *addressbook_gaim;
	GSettings *settings = g_settings_new (CONF_SCHEMA);

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	/* Save the new setting to GSettings */
	g_settings_set_boolean (settings, CONF_KEY_ENABLE_GAIM, active);

	addressbook_gaim = g_settings_get_string (
		settings, CONF_KEY_WHICH_ADDRESSBOOK_GAIM);
	gtk_widget_set_sensitive (stuff->gaim_combo_box, active);
	if (active && !addressbook_gaim) {
		selected_source = e_source_combo_box_ref_active (
			E_SOURCE_COMBO_BOX (stuff->gaim_combo_box));
		if (selected_source != NULL) {
			g_settings_set_string (
				settings, CONF_KEY_WHICH_ADDRESSBOOK_GAIM,
				e_source_get_uid (selected_source));
			g_object_unref (selected_source);
		} else {
			g_settings_set_string (
				settings, CONF_KEY_WHICH_ADDRESSBOOK_GAIM, "");
		}
	}

	g_free (addressbook_gaim);
	g_object_unref (settings);
}

static void
synchronize_button_clicked_cb (GtkWidget *button)
{
	bbdb_sync_buddy_list ();
}

static void
source_changed_cb (ESourceComboBox *source_combo_box,
                   struct bbdb_stuff *stuff)
{
	GSettings *settings;
	ESource *source;
	const gchar *uid;

	source = e_source_combo_box_ref_active (source_combo_box);
	uid = (source != NULL) ? e_source_get_uid (source) : "";

	settings = g_settings_new (CONF_SCHEMA);
	g_settings_set_string (settings, CONF_KEY_WHICH_ADDRESSBOOK, uid);
	g_object_unref (settings);

	if (source != NULL)
		g_object_unref (source);
}

static void
gaim_source_changed_cb (ESourceComboBox *source_combo_box,
                        struct bbdb_stuff *stuff)
{
	GSettings *settings;
	ESource *source;
	const gchar *uid;

	source = e_source_combo_box_ref_active (source_combo_box);
	uid = (source != NULL) ? e_source_get_uid (source) : "";

	settings = g_settings_new (CONF_SCHEMA);
	g_settings_set_string (settings, CONF_KEY_WHICH_ADDRESSBOOK_GAIM, uid);
	g_object_unref (settings);

	if (source != NULL)
		g_object_unref (source);
}

static GtkWidget *
create_addressbook_combo_box (struct bbdb_stuff *stuff,
                              gint type)
{
	EShell *shell;
	ESource *source;
	ESourceRegistry *registry;
	GtkWidget *combo_box;
	const gchar *extension_name;
	gchar *uid;
	GSettings *settings = g_settings_new (CONF_SCHEMA);

	shell = e_shell_get_default ();
	registry = e_shell_get_registry (shell);
	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	combo_box = e_source_combo_box_new (registry, extension_name);

	if (type == GAIM_ADDRESSBOOK)
		uid = g_settings_get_string (settings, CONF_KEY_WHICH_ADDRESSBOOK_GAIM);
	else
		uid = g_settings_get_string (settings, CONF_KEY_WHICH_ADDRESSBOOK);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	if (source != NULL) {
		e_source_combo_box_set_active (
			E_SOURCE_COMBO_BOX (combo_box), source);
		g_object_unref (source);
	}

	gtk_widget_show (combo_box);

	g_object_unref (settings);

	return combo_box;
}

GtkWidget *
bbdb_page_factory (EPlugin *ep,
                   EConfigHookItemFactoryData *hook_data)
{
	struct bbdb_stuff *stuff;
	EABConfigTargetPrefs *target = (EABConfigTargetPrefs *) hook_data->config->target;
	GtkWidget *page;
	GtkWidget *tab_label;
	GtkWidget *frame;
	GtkWidget *frame_label;
	GtkWidget *padding_label;
	GtkWidget *hbox;
	GtkWidget *inner_vbox;
	GtkWidget *check;
	GtkWidget *combo_box;
	GtkWidget *gaim_combo_box;
	GtkWidget *check_gaim;
	GtkWidget *label;
	GtkWidget *gaim_label;
	GtkWidget *button;
	gchar *str;
	GSettings *settings = g_settings_new (CONF_SCHEMA);

	/* A structure to pass some stuff around */
	stuff = g_new0 (struct bbdb_stuff, 1);
	stuff->target = target;

	/* Create a new notebook page */
	page = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (page), 12);
	tab_label = gtk_label_new (_("Automatic Contacts"));
	gtk_notebook_append_page (GTK_NOTEBOOK (hook_data->parent), page, tab_label);

	/* Frame */
	frame = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (page), frame, FALSE, FALSE, 0);

	/* "Automatic Contacts" */
	frame_label = gtk_label_new ("");
	str = g_strdup_printf ("<span weight=\"bold\">%s</span>", _("Automatic Contacts"));
	gtk_label_set_markup (GTK_LABEL (frame_label), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (frame_label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (frame), frame_label, FALSE, FALSE, 0);

	/* Indent/padding */
	hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (frame), hbox, FALSE, TRUE, 0);
	padding_label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), padding_label, FALSE, FALSE, 0);
	inner_vbox = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox), inner_vbox, FALSE, FALSE, 0);

	/* Enable BBDB checkbox */
	check = gtk_check_button_new_with_mnemonic (_("Create _address book entries when sending mails"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), g_settings_get_boolean (settings, CONF_KEY_ENABLE));
	g_signal_connect (
		check, "toggled",
		G_CALLBACK (enable_toggled_cb), stuff);
	gtk_box_pack_start (GTK_BOX (inner_vbox), check, FALSE, FALSE, 0);
	stuff->check = check;

	label = gtk_label_new (_("Select Address book for Automatic Contacts"));
	gtk_box_pack_start (GTK_BOX (inner_vbox), label, FALSE, FALSE, 0);

	/* Source selection combo box */
	combo_box = create_addressbook_combo_box (stuff, AUTOMATIC_CONTACTS_ADDRESSBOOK);
	g_signal_connect (
		combo_box, "changed",
		G_CALLBACK (source_changed_cb), stuff);
	gtk_widget_set_sensitive (combo_box, g_settings_get_boolean (settings, CONF_KEY_ENABLE));
	gtk_box_pack_start (GTK_BOX (inner_vbox), combo_box, FALSE, FALSE, 0);
	stuff->combo_box = combo_box;

	/* "Instant Messaging Contacts" */
	frame = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (page), frame, TRUE, TRUE, 24);

	frame_label = gtk_label_new ("");
	str = g_strdup_printf ("<span weight=\"bold\">%s</span>", _("Instant Messaging Contacts"));
	gtk_label_set_markup (GTK_LABEL (frame_label), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (frame_label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (frame), frame_label, FALSE, FALSE, 0);

	/* Indent/padding */
	hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (frame), hbox, FALSE, TRUE, 0);
	padding_label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), padding_label, FALSE, FALSE, 0);
	inner_vbox = gtk_vbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (hbox), inner_vbox, FALSE, FALSE, 0);

	/* Enable Gaim Checkbox */
	check_gaim = gtk_check_button_new_with_mnemonic (_("_Synchronize contact info and images from Pidgin buddy list"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_gaim), g_settings_get_boolean (settings, CONF_KEY_ENABLE_GAIM));
	g_signal_connect (
		check_gaim, "toggled",
		G_CALLBACK (enable_gaim_toggled_cb), stuff);
	gtk_box_pack_start (GTK_BOX (inner_vbox), check_gaim, FALSE, FALSE, 0);
	stuff->check_gaim = check_gaim;

	gaim_label = gtk_label_new (_("Select Address book for Pidgin buddy list"));
	gtk_box_pack_start (GTK_BOX (inner_vbox), gaim_label, FALSE, FALSE, 0);

	/* Gaim Source Selection Combo Box */
	gaim_combo_box = create_addressbook_combo_box (stuff, GAIM_ADDRESSBOOK);
	g_signal_connect (
		gaim_combo_box, "changed",
		G_CALLBACK (gaim_source_changed_cb), stuff);
	gtk_widget_set_sensitive (gaim_combo_box, g_settings_get_boolean (settings, CONF_KEY_ENABLE_GAIM));
	gtk_box_pack_start (GTK_BOX (inner_vbox), gaim_combo_box, FALSE, FALSE, 0);
	stuff->gaim_combo_box = gaim_combo_box;

	/* Synchronize now button. */
	button = gtk_button_new_with_mnemonic (_("Synchronize with _buddy list now"));
	g_signal_connect (
		button, "clicked",
		G_CALLBACK (synchronize_button_clicked_cb), stuff);
	gtk_box_pack_start (GTK_BOX (inner_vbox), button, FALSE, FALSE, 0);

	/* Clean up */
	g_signal_connect (
		page, "destroy",
		G_CALLBACK (cleanup_cb), stuff);

	gtk_widget_show_all (page);

	g_object_unref (settings);

	return page;
}

static void
cleanup_cb (GObject *o,
            gpointer data)
{
	struct bbdb_stuff *stuff = data;

	g_free (stuff);
}
