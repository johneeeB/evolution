/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * gal-define-views-dialog.c
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include "gal-view-instance-save-as-dialog.h"

#include <libgnomeui/gnome-dialog.h>
#include "gal-define-views-model.h"
#include "gal-view-new-dialog.h"
#include <gal/e-table/e-table-scrolled.h>
#include <gal/util/e-i18n.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkbox.h>

static GnomeDialogClass *parent_class = NULL;
#define PARENT_TYPE gnome_dialog_get_type()

/* The arguments we take */
enum {
	ARG_0,
	ARG_INSTANCE,
};

typedef struct {
	char         *title;
	ETableModel  *model;
	GalViewInstanceSaveAsDialog *names;
} GalViewInstanceSaveAsDialogChild;


/* Static functions */
static void
gal_view_instance_save_as_dialog_set_instance(GalViewInstanceSaveAsDialog *dialog,
					      GalViewInstance *instance)
{
	dialog->instance = instance;
	if (dialog->model) {
		gtk_object_set(GTK_OBJECT(dialog->model),
			       "collection", instance ? instance->collection : NULL,
			       NULL);
	}
}

static void
gvisad_setup_radio_buttons (GalViewInstanceSaveAsDialog *dialog)
{
	GtkWidget   *radio_replace = glade_xml_get_widget (dialog->gui, "radiobutton-replace");
	GtkWidget   *radio_create  = glade_xml_get_widget (dialog->gui, "radiobutton-create" );
	GtkWidget   *widget;
	GtkNotebook *notebook      = GTK_NOTEBOOK (glade_xml_get_widget (dialog->gui, "notebook-help"));

	widget = glade_xml_get_widget (dialog->gui, "custom-replace");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_replace))) {
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_notebook_set_page (notebook, 0);
		dialog->toggle = GAL_VIEW_INSTANCE_SAVE_AS_DIALOG_TOGGLE_REPLACE;
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	widget = glade_xml_get_widget (dialog->gui, "entry-create");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_create))) {
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_notebook_set_page (notebook, 1);
		dialog->toggle = GAL_VIEW_INSTANCE_SAVE_AS_DIALOG_TOGGLE_CREATE;
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}
}

static void
gvisad_radio_toggled (GtkWidget *widget, GalViewInstanceSaveAsDialog *dialog)
{
	gvisad_setup_radio_buttons (dialog);
}

static void
gvisad_connect_signal(GalViewInstanceSaveAsDialog *dialog, char *widget_name, char *signal, GtkSignalFunc handler)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget(dialog->gui, widget_name);

	if (widget)
		g_signal_connect (G_OBJECT (widget), signal, G_CALLBACK (handler), dialog);
}

/* Method override implementations */
static void
gal_view_instance_save_as_dialog_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GalViewInstanceSaveAsDialog *dialog;

	dialog = GAL_VIEW_INSTANCE_SAVE_AS_DIALOG (o);
	
	switch (arg_id){
	case ARG_INSTANCE:
		if (GTK_VALUE_OBJECT(*arg))
			gal_view_instance_save_as_dialog_set_instance(dialog, GAL_VIEW_INSTANCE(GTK_VALUE_OBJECT(*arg)));
		else
			gal_view_instance_save_as_dialog_set_instance(dialog, NULL);
		break;

	default:
		return;
	}
}

static void
gal_view_instance_save_as_dialog_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	GalViewInstanceSaveAsDialog *dialog;

	dialog = GAL_VIEW_INSTANCE_SAVE_AS_DIALOG (object);

	switch (arg_id) {
	case ARG_INSTANCE:
		if (dialog->instance)
			GTK_VALUE_OBJECT(*arg) = GTK_OBJECT(dialog->instance);
		else
			GTK_VALUE_OBJECT(*arg) = NULL;
		break;

	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static void
gal_view_instance_save_as_dialog_destroy (GtkObject *object)
{
	GalViewInstanceSaveAsDialog *gal_view_instance_save_as_dialog = GAL_VIEW_INSTANCE_SAVE_AS_DIALOG(object);

	gtk_object_unref(GTK_OBJECT(gal_view_instance_save_as_dialog->gui));

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

/* Init functions */
static void
gal_view_instance_save_as_dialog_class_init (GalViewInstanceSaveAsDialogClass *klass)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass*) klass;

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->set_arg = gal_view_instance_save_as_dialog_set_arg;
	object_class->get_arg = gal_view_instance_save_as_dialog_get_arg;
	object_class->destroy = gal_view_instance_save_as_dialog_destroy;

	gtk_object_add_arg_type("GalViewInstanceSaveAsDialog::instance", GAL_VIEW_INSTANCE_TYPE,
				GTK_ARG_READWRITE, ARG_INSTANCE);
}

static void
gal_view_instance_save_as_dialog_init (GalViewInstanceSaveAsDialog *dialog)
{
	GladeXML *gui;
	GtkWidget *widget;
	GtkWidget *etable;

	dialog->instance = NULL;

	gui = glade_xml_new_with_domain (GAL_GLADEDIR "/gal-view-instance-save-as-dialog.glade", NULL, PACKAGE);
	dialog->gui = gui;

	widget = glade_xml_get_widget(gui, "table-top");
	if (!widget) {
		return;
	}
	gtk_widget_ref(widget);
	gtk_widget_unparent(widget);
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), widget, TRUE, TRUE, 0);
	gtk_widget_unref(widget);

	gnome_dialog_append_buttons(GNOME_DIALOG(dialog),
				    GNOME_STOCK_BUTTON_OK,
				    GNOME_STOCK_BUTTON_CANCEL,
				    NULL);

	gvisad_connect_signal(dialog, "radiobutton-replace", "toggled", GTK_SIGNAL_FUNC(gvisad_radio_toggled));
	gvisad_connect_signal(dialog, "radiobutton-create",  "toggled", GTK_SIGNAL_FUNC(gvisad_radio_toggled));

	dialog->model = NULL;
	etable = glade_xml_get_widget(dialog->gui, "custom-replace");
	if (etable) {
		dialog->model = gtk_object_get_data(GTK_OBJECT(etable), "GalViewInstanceSaveAsDialog::model");
		gtk_object_set(GTK_OBJECT(dialog->model),
			       "collection", dialog->instance ? dialog->instance->collection : NULL,
			       NULL);
	}
	
	gvisad_setup_radio_buttons (dialog);
	gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, TRUE, FALSE);
}


/* For use from libglade. */
/* ETable creation */
#define SPEC "<ETableSpecification no-header=\"true\" cursor-mode=\"line\" draw-grid=\"false\" selection-mode=\"single\" gettext-domain=\"" E_I18N_DOMAIN "\">" \
	     "<ETableColumn model_col= \"0\" _title=\"Name\" expansion=\"1.0\" minimum_width=\"18\" resizable=\"true\" cell=\"string\" compare=\"string\"/>" \
             "<ETableState> <column source=\"0\"/> <grouping> </grouping> </ETableState>" \
	     "</ETableSpecification>"

GtkWidget *gal_view_instance_save_as_dialog_create_etable(char *name, char *string1, char *string2, int int1, int int2);

GtkWidget *
gal_view_instance_save_as_dialog_create_etable(char *name, char *string1, char *string2, int int1, int int2)
{
	GtkWidget *table;
	ETableModel *model;
	model = gal_define_views_model_new();
	table = e_table_scrolled_new(model, NULL, SPEC, NULL);
	gtk_object_set_data(GTK_OBJECT(table), "GalViewInstanceSaveAsDialog::model", model);
	return table;
}

/* External methods */
/**
 * gal_view_instance_save_as_dialog_new
 *
 * Returns a new dialog for defining views.
 *
 * Returns: The GalViewInstanceSaveAsDialog.
 */
GtkWidget*
gal_view_instance_save_as_dialog_new (GalViewInstance *instance)
{
	GtkWidget *widget = GTK_WIDGET (gtk_type_new (gal_view_instance_save_as_dialog_get_type ()));
	gal_view_instance_save_as_dialog_set_instance(GAL_VIEW_INSTANCE_SAVE_AS_DIALOG (widget), instance);
	return widget;
}

GtkType
gal_view_instance_save_as_dialog_get_type (void)
{
	static GtkType type = 0;

	if (!type) {
		static const GtkTypeInfo info =
		{
			"GalViewInstanceSaveAsDialog",
			sizeof (GalViewInstanceSaveAsDialog),
			sizeof (GalViewInstanceSaveAsDialogClass),
			(GtkClassInitFunc) gal_view_instance_save_as_dialog_class_init,
			(GtkObjectInitFunc) gal_view_instance_save_as_dialog_init,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		type = gtk_type_unique (PARENT_TYPE, &info);
	}

	return type;
}

void
gal_view_instance_save_as_dialog_save (GalViewInstanceSaveAsDialog *dialog)
{
	GalView *view = gal_view_instance_get_current_view (dialog->instance);
	GtkWidget *widget;
	const char *title;
	int n;
	const char *id = NULL;
	switch (dialog->toggle) {
	case GAL_VIEW_INSTANCE_SAVE_AS_DIALOG_TOGGLE_REPLACE:
		widget = glade_xml_get_widget(dialog->gui, "custom-replace");
		if (widget && E_IS_TABLE_SCROLLED (widget)) {
			n = e_table_get_cursor_row (e_table_scrolled_get_table (E_TABLE_SCROLLED (widget)));
			id = gal_view_collection_set_nth_view (dialog->instance->collection, n, view);
			gal_view_collection_save (dialog->instance->collection);
		}
		break;
	case GAL_VIEW_INSTANCE_SAVE_AS_DIALOG_TOGGLE_CREATE:
		widget = glade_xml_get_widget(dialog->gui, "entry-create");
		if (widget && GTK_IS_ENTRY (widget)) {
			title = gtk_entry_get_text (GTK_ENTRY (widget));
			id = gal_view_collection_append_with_title (dialog->instance->collection, title, view);
			gal_view_collection_save (dialog->instance->collection);
		}
		break;
	}

	if (id) {
		gal_view_instance_set_current_view_id (dialog->instance, id);
	}
}
