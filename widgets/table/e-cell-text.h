/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-cell-text.h: Text cell renderer.
 * Copyright 1999, 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza <miguel@ximian.com>
 *   Chris Lahey <clahey@ximian.com>
 *
 * A lot of code taken from:
 * 
 * Text item type for GnomeCanvas widget
 *
 * GnomeCanvas is basically a port of the Tk toolkit's most excellent
 * canvas widget.  Tk is copyrighted by the Regents of the University
 * of California, Sun Microsystems, and other parties.
 *
 * Copyright (C) 1998 The Free Software Foundation
 *
 * Author: Federico Mena <federico@nuclecu.unam.mx>
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

#ifndef _E_CELL_TEXT_H_
#define _E_CELL_TEXT_H_

#include <libgnomecanvas/gnome-canvas.h>
#include <gal/e-table/e-cell.h>

G_BEGIN_DECLS

#define E_CELL_TEXT_TYPE        (e_cell_text_get_type ())
#define E_CELL_TEXT(o)          (GTK_CHECK_CAST ((o), E_CELL_TEXT_TYPE, ECellText))
#define E_CELL_TEXT_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_CELL_TEXT_TYPE, ECellTextClass))
#define E_IS_CELL_TEXT(o)       (GTK_CHECK_TYPE ((o), E_CELL_TEXT_TYPE))
#define E_IS_CELL_TEXT_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_CELL_TEXT_TYPE))

typedef struct {
	ECell parent;

	GtkJustification  justify;
	char             *font_name;

	double x, y;			/* Position at anchor */

	gulong pixel;			/* Fill color */

	/* Clip handling */
	char *ellipsis;                 /* The ellipsis characters.  NULL = "...". */

	guint use_ellipsis : 1;         /* Whether to use the ellipsis. */
	guint editable : 1;		/* Whether the text can be edited. */
	
	int strikeout_column;
	int bold_column;

	/* This column in the ETable should return a string specifying a color,
	   either a color name like "red" or a color spec like "rgb:F/0/0".
	   See the XParseColor man page for the formats available. */
	int color_column;
	int bg_color_column;

	/* This stores the colors we have allocated. */
	GHashTable *colors;
} ECellText;

typedef struct {
	ECellClass parent_class;

	char *(*get_text)  (ECellText *cell, ETableModel *model, int col, int row);
	void  (*free_text) (ECellText *cell, char *text);
	void  (*set_value) (ECellText *cell, ETableModel *model, int col, int row, const char *text);
} ECellTextClass;

GtkType    e_cell_text_get_type (void);
ECell     *e_cell_text_new      (const char *fontname, GtkJustification justify);
ECell     *e_cell_text_construct(ECellText *cell, const char *fontname, GtkJustification justify);

/* Gets the value from the model and converts it into a string. In ECellText
   itself, the value is assumed to be a char* and so needs no conversion.
   In subclasses the ETableModel value may be a more complicated datatype. */
char	  *e_cell_text_get_text (ECellText *cell, ETableModel *model, int col, int row);

/* Frees the value returned by e_cell_text_get_text(). */
void	   e_cell_text_free_text (ECellText *cell, char *text);

/* Sets the ETableModel value, based on the given string. */
void	   e_cell_text_set_value (ECellText *cell, ETableModel *model, int col, int row, const char *text);

G_END_DECLS

#endif /* _E_CELL_TEXT_H_ */


