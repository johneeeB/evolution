/*
 * e-memo-shell-backend.c
 *
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
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-memo-shell-backend.h"

#include <string.h>
#include <glib/gi18n.h>
#include <libecal/e-cal-client.h>
#include <libedataserver/e-url.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>
#include <libedataserver/e-source-group.h>
#include <libedataserverui/e-client-utils.h>

#include "shell/e-shell.h"
#include "shell/e-shell-backend.h"
#include "shell/e-shell-window.h"

#include "calendar/gui/comp-util.h"
#include "calendar/gui/dialogs/calendar-setup.h"
#include "calendar/gui/dialogs/memo-editor.h"

#include "e-memo-shell-migrate.h"
#include "e-memo-shell-view.h"

#define WEB_BASE_URI		"webcal://"
#define PERSONAL_RELATIVE_URI	"system"

struct _EMemoShellBackendPrivate {
	ESourceList *source_list;
};

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

static gpointer parent_class;
static GType memo_shell_backend_type;

static void
memo_shell_backend_ensure_sources (EShellBackend *shell_backend)
{
	/* XXX This is basically the same algorithm across all modules.
	 *     Maybe we could somehow integrate this into EShellBackend? */

	EMemoShellBackend *memo_shell_backend;
	ESourceGroup *on_this_computer;
	ESourceList *source_list;
	ESource *personal;
	EShell *shell;
	EShellSettings *shell_settings;
	GSList *sources, *iter;
	const gchar *name;
	gboolean save_list = FALSE;
	GError *error = NULL;

	personal = NULL;

	memo_shell_backend = E_MEMO_SHELL_BACKEND (shell_backend);

	shell = e_shell_backend_get_shell (shell_backend);
	shell_settings = e_shell_get_shell_settings (shell);

	e_cal_client_get_sources (
		&memo_shell_backend->priv->source_list,
		E_CAL_CLIENT_SOURCE_TYPE_MEMOS, &error);

	if (error != NULL) {
		g_warning (
			"%s: Could not get memo sources: %s",
			G_STRFUNC, error->message);
		g_error_free (error);
		return;
	}

	source_list = memo_shell_backend->priv->source_list;

	on_this_computer = e_source_list_ensure_group (
		source_list, _("On This Computer"), "local:", TRUE);
	e_source_list_ensure_group (
		source_list, _("On The Web"), "webcal://", FALSE);

	g_return_if_fail (on_this_computer);

	sources = e_source_group_peek_sources (on_this_computer);

	/* Make sure this group includes a "Personal" source. */
	for (iter = sources; iter != NULL; iter = iter->next) {
		ESource *source = iter->data;
		const gchar *relative_uri;

		relative_uri = e_source_peek_relative_uri (source);
		if (g_strcmp0 (relative_uri, "system") == 0) {
			personal = source;
			break;
		}
	}

	name = _("Personal");

	if (personal == NULL) {
		ESource *source;
		GSList *selected;
		gchar *primary;

		source = e_source_new (name, "system");
		e_source_set_color_spec (source, "#BECEDD");
		e_source_group_add_source (on_this_computer, source, -1);
		g_object_unref (source);
		save_list = TRUE;

		primary = e_shell_settings_get_string (
			shell_settings, "cal-primary-memo-list");

		selected = e_memo_shell_backend_get_selected_memo_lists (
			memo_shell_backend);

		if (primary == NULL && selected == NULL) {
			const gchar *uid;

			uid = e_source_peek_uid (source);
			selected = g_slist_prepend (NULL, g_strdup (uid));

			e_shell_settings_set_string (
				shell_settings, "cal-primary-memo-list", uid);
			e_memo_shell_backend_set_selected_memo_lists (
				memo_shell_backend, selected);
		}

		g_slist_foreach (selected, (GFunc) g_free, NULL);
		g_slist_free (selected);
		g_free (primary);
	} else {
		/* Force the source name to the current locale. */
		e_source_set_name (personal, name);
	}

	g_object_unref (on_this_computer);

	if (save_list)
		e_source_list_sync (source_list, NULL);
}

static void
memo_shell_backend_new_memo (ESource *source,
                             GAsyncResult *result,
                             EShell *shell,
                             CompEditorFlags flags)
{
	EClient *client = NULL;
	ECalClient *cal_client;
	ECalComponent *comp;
	CompEditor *editor;
	GError *error = NULL;

	e_client_utils_open_new_finish (source, result, &client, &error);

	/* XXX Handle errors better. */
	if (error != NULL) {
		g_warn_if_fail (client == NULL);
		g_warning (
			"%s: Failed to open '%s': %s",
			G_STRFUNC, e_source_peek_name (source),
			error->message);
		g_error_free (error);
		return;
	}

	g_return_if_fail (E_IS_CAL_CLIENT (client));

	cal_client = E_CAL_CLIENT (client);
	comp = cal_comp_memo_new_with_defaults (cal_client);
	cal_comp_update_time_by_active_window (comp, shell);
	editor = memo_editor_new (cal_client, shell, flags);
	comp_editor_edit_comp (editor, comp);

	gtk_window_present (GTK_WINDOW (editor));

	g_object_unref (comp);
	g_object_unref (client);
}

static void
memo_shell_backend_memo_new_cb (GObject *source_object,
                                GAsyncResult *result,
                                gpointer shell)
{
	CompEditorFlags flags = 0;

	flags |= COMP_EDITOR_NEW_ITEM;

	memo_shell_backend_new_memo (
		E_SOURCE (source_object), result, shell, flags);

	g_object_unref (shell);
}

static void
memo_shell_backend_memo_shared_new_cb (GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer shell)
{
	CompEditorFlags flags = 0;

	flags |= COMP_EDITOR_NEW_ITEM;
	flags |= COMP_EDITOR_IS_SHARED;
	flags |= COMP_EDITOR_USER_ORG;

	memo_shell_backend_new_memo (
		E_SOURCE (source_object), result, shell, flags);

	g_object_unref (shell);
}

static void
action_memo_new_cb (GtkAction *action,
                    EShellWindow *shell_window)
{
	EShell *shell;
	EShellBackend *shell_backend;
	EShellSettings *shell_settings;
	ESource *source = NULL;
	ESourceList *source_list;
	const gchar *action_name;
	gchar *uid;

	/* This callback is used for both memos and shared memos. */

	shell = e_shell_window_get_shell (shell_window);
	shell_settings = e_shell_get_shell_settings (shell);
	shell_backend = e_shell_get_backend_by_name (shell, "memos");

	g_object_get (shell_backend, "source-list", &source_list, NULL);
	g_return_if_fail (E_IS_SOURCE_LIST (source_list));

	uid = e_shell_settings_get_string (
		shell_settings, "cal-primary-memo-list");

	if (uid != NULL) {
		source = e_source_list_peek_source_by_uid (source_list, uid);
		g_free (uid);
	}

	if (source == NULL)
		source = e_source_list_peek_default_source (source_list);

	g_return_if_fail (E_IS_SOURCE (source));

	/* Use a callback function appropriate for the action.
	 * FIXME Need to obtain a better default time zone. */
	action_name = gtk_action_get_name (action);
	if (g_strcmp0 (action_name, "memo-shared-new") == 0)
		e_client_utils_open_new (source, E_CLIENT_SOURCE_TYPE_MEMOS, FALSE, NULL,
			e_client_utils_authenticate_handler, GTK_WINDOW (shell_window),
			memo_shell_backend_memo_shared_new_cb, g_object_ref (shell));
	else
		e_client_utils_open_new (source, E_CLIENT_SOURCE_TYPE_MEMOS, FALSE, NULL,
			e_client_utils_authenticate_handler, GTK_WINDOW (shell_window),
			memo_shell_backend_memo_new_cb, g_object_ref (shell));

	g_object_unref (source_list);
}

static void
action_memo_list_new_cb (GtkAction *action,
                         EShellWindow *shell_window)
{
	calendar_setup_new_memo_list (GTK_WINDOW (shell_window));
}

static GtkActionEntry item_entries[] = {

	{ "memo-new",
	  "stock_insert-note",
	  NC_("New", "Mem_o"),
	  "<Shift><Control>o",
	  N_("Create a new memo"),
	  G_CALLBACK (action_memo_new_cb) },

	{ "memo-shared-new",
	  "stock_insert-note",
	  NC_("New", "_Shared Memo"),
	  "<Shift><Control>h",
	  N_("Create a new shared memo"),
	  G_CALLBACK (action_memo_new_cb) }
};

static GtkActionEntry source_entries[] = {

	{ "memo-list-new",
	  "stock_notes",
	  NC_("New", "Memo Li_st"),
	  NULL,
	  N_("Create a new memo list"),
	  G_CALLBACK (action_memo_list_new_cb) }
};

static gboolean
memo_shell_backend_handle_uri_cb (EShellBackend *shell_backend,
                                  const gchar *uri)
{
	EShell *shell;
	CompEditor *editor;
	CompEditorFlags flags = 0;
	ECalClient *client;
	ECalComponent *comp;
	ESource *source;
	ESourceList *source_list;
	ECalClientSourceType source_type;
	EUri *euri;
	icalcomponent *icalcomp;
	const gchar *cp;
	gchar *source_uid = NULL;
	gchar *comp_uid = NULL;
	gchar *comp_rid = NULL;
	gboolean handled = FALSE;
	GError *error = NULL;

	source_type = E_CAL_CLIENT_SOURCE_TYPE_MEMOS;
	shell = e_shell_backend_get_shell (shell_backend);

	if (strncmp (uri, "memo:", 5) != 0)
		return FALSE;

	euri = e_uri_new (uri);
	cp = euri->query;
	if (cp == NULL)
		goto exit;

	while (*cp != '\0') {
		gchar *header;
		gchar *content;
		gsize header_len;
		gsize content_len;

		header_len = strcspn (cp, "=&");

		/* If it's malformed, give up. */
		if (cp[header_len] != '=')
			break;

		header = (gchar *) cp;
		header[header_len] = '\0';
		cp += header_len + 1;

		content_len = strcspn (cp, "&");

		content = g_strndup (cp, content_len);
		if (g_ascii_strcasecmp (header, "source-uid") == 0)
			source_uid = g_strdup (content);
		else if (g_ascii_strcasecmp (header, "comp-uid") == 0)
			comp_uid = g_strdup (content);
		else if (g_ascii_strcasecmp (header, "comp-rid") == 0)
			comp_rid = g_strdup (content);
		g_free (content);

		cp += content_len;
		if (*cp == '&') {
			cp++;
			if (strcmp (cp, "amp;") == 0)
				cp += 4;
		}
	}

	if (source_uid == NULL || comp_uid == NULL)
		goto exit;

	/* URI is valid, so consider it handled.  Whether
	 * we successfully open it is another matter... */
	handled = TRUE;

	e_cal_client_get_sources (&source_list, source_type, &error);

	if (error != NULL) {
		g_warning (
			"%s: Could not get memo sources: %s",
			G_STRFUNC, error->message);
		g_clear_error (&error);
		goto exit;
	}

	source = e_source_list_peek_source_by_uid (source_list, source_uid);
	if (source == NULL) {
		g_printerr ("No source for UID '%s'\n", source_uid);
		g_object_unref (source_list);
		goto exit;
	}

	client = e_cal_client_new (source, source_type, &error);

	if (client != NULL) {
		g_signal_connect (
			client, "authenticate",
			G_CALLBACK (e_client_utils_authenticate_handler), NULL);
		e_client_open_sync (E_CLIENT (client), TRUE, NULL, &error);
	}

	if (error != NULL) {
		g_warning (
			"%s: Failed to create/open client: %s",
			G_STRFUNC, error->message);
		if (client != NULL)
			g_object_unref (client);
		g_object_unref (source_list);
		g_error_free (error);
		goto exit;
	}

	/* XXX Copied from e_memo_shell_view_open_memo().
	 *     Clearly a new utility function is needed. */

	editor = comp_editor_find_instance (comp_uid);

	if (editor != NULL)
		goto present;

	e_cal_client_get_object_sync (
		client, comp_uid, comp_rid, &icalcomp, NULL, &error);

	if (error != NULL) {
		g_warning (
			"%s: Failed to get object: %s",
			G_STRFUNC, error->message);
		g_object_unref (source_list);
		g_object_unref (client);
		g_error_free (error);
		goto exit;
	}

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		g_warning ("%s: Failed to set icalcomp to comp\n", G_STRFUNC);
		icalcomponent_free (icalcomp);
		icalcomp = NULL;
	}

	if (e_cal_component_has_organizer (comp))
		flags |= COMP_EDITOR_IS_SHARED;

	if (itip_organizer_is_user (comp, client))
		flags |= COMP_EDITOR_USER_ORG;

	editor = memo_editor_new (client, shell, flags);
	comp_editor_edit_comp (editor, comp);

	g_object_unref (comp);

present:
	gtk_window_present (GTK_WINDOW (editor));

	g_object_unref (source_list);
	g_object_unref (client);

exit:
	g_free (source_uid);
	g_free (comp_uid);
	g_free (comp_rid);

	e_uri_free (euri);

	return handled;
}

static void
memo_shell_backend_window_added_cb (EShellBackend *shell_backend,
                                    GtkWindow *window)
{
	const gchar *module_name;

	if (!E_IS_SHELL_WINDOW (window))
		return;

	module_name = E_SHELL_BACKEND_GET_CLASS (shell_backend)->name;

	e_shell_window_register_new_item_actions (
		E_SHELL_WINDOW (window), module_name,
		item_entries, G_N_ELEMENTS (item_entries));

	e_shell_window_register_new_source_actions (
		E_SHELL_WINDOW (window), module_name,
		source_entries, G_N_ELEMENTS (source_entries));
}

static void
memo_shell_backend_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (
				value,
				e_memo_shell_backend_get_source_list (
				E_MEMO_SHELL_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
memo_shell_backend_dispose (GObject *object)
{
	EMemoShellBackendPrivate *priv;

	priv = E_MEMO_SHELL_BACKEND (object)->priv;

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
memo_shell_backend_constructed (GObject *object)
{
	EShell *shell;
	EShellBackend *shell_backend;

	shell_backend = E_SHELL_BACKEND (object);
	shell = e_shell_backend_get_shell (shell_backend);

	memo_shell_backend_ensure_sources (shell_backend);

	g_signal_connect_swapped (
		shell, "handle-uri",
		G_CALLBACK (memo_shell_backend_handle_uri_cb),
		shell_backend);

	g_signal_connect_swapped (
		shell, "window-added",
		G_CALLBACK (memo_shell_backend_window_added_cb),
		shell_backend);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
memo_shell_backend_class_init (EMemoShellBackendClass *class)
{
	GObjectClass *object_class;
	EShellBackendClass *shell_backend_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMemoShellBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = memo_shell_backend_get_property;
	object_class->dispose = memo_shell_backend_dispose;
	object_class->constructed = memo_shell_backend_constructed;

	shell_backend_class = E_SHELL_BACKEND_CLASS (class);
	shell_backend_class->shell_view_type = E_TYPE_MEMO_SHELL_VIEW;
	shell_backend_class->name = "memos";
	shell_backend_class->aliases = "";
	shell_backend_class->schemes = "memo";
	shell_backend_class->sort_order = 600;
	shell_backend_class->preferences_page = "calendar-and-tasks";
	shell_backend_class->start = NULL;
	shell_backend_class->migrate = e_memo_shell_backend_migrate;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			"Source List",
			"The registry of memo lists",
			E_TYPE_SOURCE_LIST,
			G_PARAM_READABLE));
}

static void
memo_shell_backend_init (EMemoShellBackend *memo_shell_backend)
{
	memo_shell_backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		memo_shell_backend, E_TYPE_MEMO_SHELL_BACKEND,
		EMemoShellBackendPrivate);
}

GType
e_memo_shell_backend_get_type (void)
{
	return memo_shell_backend_type;
}

void
e_memo_shell_backend_register_type (GTypeModule *type_module)
{
	const GTypeInfo type_info = {
		sizeof (EMemoShellBackendClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc) memo_shell_backend_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (EMemoShellBackend),
		0,     /* n_preallocs */
		(GInstanceInitFunc) memo_shell_backend_init,
		NULL   /* value_table */
	};

	memo_shell_backend_type = g_type_module_register_type (
		type_module, E_TYPE_SHELL_BACKEND,
		"EMemoShellBackend", &type_info, 0);
}

ESourceList *
e_memo_shell_backend_get_source_list (EMemoShellBackend *memo_shell_backend)
{
	g_return_val_if_fail (
		E_IS_MEMO_SHELL_BACKEND (memo_shell_backend), NULL);

	return memo_shell_backend->priv->source_list;
}

GSList *
e_memo_shell_backend_get_selected_memo_lists (EMemoShellBackend *memo_shell_backend)
{
	GSettings *settings;
	char **strv;
	gint i;
	GSList *selected_memo_lists = NULL;

	g_return_val_if_fail (
		E_IS_MEMO_SHELL_BACKEND (memo_shell_backend), NULL);

	settings = g_settings_new ("org.gnome.evolution.calendar");
	strv = g_settings_get_strv (settings, "selected-memos");
	g_object_unref (settings);


	if (strv != NULL) {
		for (i = 0; strv[i] != NULL; i++)
			selected_memo_lists = g_slist_append (selected_memo_lists, g_strdup (strv[i]));

		g_strfreev (strv);
	}

	return selected_memo_lists;
}

void
e_memo_shell_backend_set_selected_memo_lists (EMemoShellBackend *memo_shell_backend,
                                              GSList *selected_memo_lists)
{
	GSettings *settings;
	GSList *l;
	GPtrArray *array = g_ptr_array_new ();

	g_return_if_fail (E_IS_MEMO_SHELL_BACKEND (memo_shell_backend));

	for (l = selected_memo_lists; l != NULL; l = l->next)
		g_ptr_array_add (array, l->data);

	settings = g_settings_new ("org.gnome.evolution.calendar");
	g_settings_set_strv (settings, "selected-memos", array->pdata);
	g_object_unref (settings);

	g_ptr_array_free (array, FALSE);
}
