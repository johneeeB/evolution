/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Evolution calendar - Print support
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *          Federico Mena-Quintero <federico@ximian.com>
 *	    Damon Chaplin <damon@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include <gtk/gtkradiobutton.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-uidefs.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <libgnomeprint/gnome-print.h>  
#include <libgnomeprint/gnome-print-paper.h>
#include <libgnomeprint/gnome-print-job.h>
#include <libgnomeprintui/gnome-print-job-preview.h>
#include <libgnomeprintui/gnome-print-paper-selector.h>
#include <libgnomeprintui/gnome-print-preview.h>
#include <libgnomeprintui/gnome-print-dialog.h>
#include <libedataserver/e-time-utils.h>
#include <libedataserver/e-data-server-util.h>
#include <e-util/e-util.h>
#include <e-util/e-dialog-widgets.h>
#include <e-util/e-print.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal-component.h>
#include "calendar-commands.h"
#include "calendar-config.h"
#include "e-cal-model.h"
#include "e-day-view.h"
#include "e-day-view-layout.h"
#include "e-week-view.h"
#include "e-week-view-layout.h"
#include "gnome-cal.h"
#include "print.h"

#include <libgnomeprint/gnome-print-pango.h>
#include <glade/glade-xml.h>
#include <gtk/gtkprintunixdialog.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkpagesetupunixdialog.h>
#include <gtk/gtkprinter.h>


typedef struct EvoCalendarPrintRenderer EvoCalendarPrintRenderer;
typedef struct PrintCompItem PrintCompItem;
typedef struct PrintTableItem PrintTableItem;
typedef struct PrintCalItem PrintCalItem;

struct EvoCalendarPrintRenderer
{
	GtkPrintContext *pc;

	/* keep one of these around: */
	PangoLayout *pl;
};

struct PrintCompItem
{
   EvoCalendarPrintRenderer *pr;
   struct ECal *client;
   struct ECalComponent *comp;
   double r,l,b,t;
};

struct PrintCalItem
{
   EvoCalendarPrintRenderer *pr;
   struct GnomeCalendar *gcal; 
   PrintView default_view; 
   double r,l,b,t;	
   time_t date;
};

struct PrintTableItem
{
	struct  EPrintable *printable;
	double left_margin,bottom_margin,paper_width,paper_height;
	GtkPrintContext *context;
};

static PrintCompItem *
evo_calendar_print_data_new (GtkPrintContext *pc, PrintCompItem *pci);

static void 
comp_draw_page (GtkPrintOperation *print, 
		GtkPrintContext *context,
		gint page_nr, 
		PrintCompItem *pci);
static void
cal_draw_page (GtkPrintOperation *print,
	       GtkPrintContext *context,
	       gint page_nr,
	       PrintCalItem *pcali);
static void 
table_draw_page (GtkPrintOperation *print,
		 GtkPrintContext *context,
		 gint page_nr1,
		 PrintTableItem *pti); 

static double
evo_calendar_print_renderer_get_width (EvoCalendarPrintRenderer *pr,
				       PangoFontDescription *font, 
				       const char *text);

GObject* create_custom_widget (GtkPrintOperation *print, GtkWidget *range);
static void apply_custom_widget (GtkPrintOperation *print, GtkWidget *dialog, PrintCalItem *cpi);
static PrintCalItem *
evo_calendar_print_cal_data_new (GtkPrintContext *pc, PrintCalItem *pcali)
{
	g_return_val_if_fail (pc, NULL);
	
	pcali->pr = g_new0 (EvoCalendarPrintRenderer, 1);
	pcali->pr->pc = pc;	
	pcali->pr->pl = gtk_print_context_create_pango_layout (pc);	
	
	return pcali;
}
static PrintCompItem *
evo_calendar_print_data_new (GtkPrintContext *pc,PrintCompItem *pci)
{
	g_return_val_if_fail (pc, NULL);
	
	pci->pr = g_new0 (EvoCalendarPrintRenderer, 1);
	pci->pr->pc = pc; 
	pci->pr->pl = gtk_print_context_create_pango_layout (pc);

	return pci;
}

static void 
evo_calendar_print_data_free (EvoCalendarPrintRenderer *pr)
{
	g_return_if_fail (pr);

	g_assert (pr->pl);
	g_object_unref (G_OBJECT (pr->pl));

	g_free (pr);
}


static double
evo_calendar_print_renderer_get_width (EvoCalendarPrintRenderer *pr,
				       PangoFontDescription *font, 
				       const char *text)
{
	int w, h;

	pango_layout_set_font_description (pr->pl, font);
	pango_layout_set_text (pr->pl, text, -1);
	pango_layout_set_indent (pr->pl, 0);
	pango_layout_get_size (pr->pl, &w, &h);

	return w/(double)PANGO_SCALE;
}


static double
get_font_size (PangoFontDescription *font)
{
	g_return_val_if_fail (font, 0.0);

	return ((double)pango_font_description_get_size (font)/(double)PANGO_SCALE);
}



/*
 * Note that most dimensions are in points (1/72 of an inch) since that is
 * what gnome-print uses.
 */

/* GtkHTML prints using a fixed margin. It has code to get the margins from
 * gnome-print keys, but it's commented out. The corresponding code here
 * doesn't seem to work either (getting zero margins), so we adopt
 * gtkhtml's cheat. */

#define TEMP_MARGIN .05

/* The fonts to use */
#define REGULAR_FONT "Sans Regular"
#define BOLD_FONT    "Sans Bold"

/* The font size to use for normal text. */
#define DAY_NORMAL_FONT_SIZE	12
#define WEEK_NORMAL_FONT_SIZE	12
#define MONTH_NORMAL_FONT_SIZE	8

/* The height of the header bar across the top of the Day, Week & Month views,
   which contains the dates shown and the 2 small calendar months. */
#define HEADER_HEIGHT		80

/* The width of the small calendar months, the space from the right edge of
   the header rectangle, and the space between the months. */
#define SMALL_MONTH_WIDTH	80
#define SMALL_MONTH_PAD		4
#define SMALL_MONTH_SPACING	12

/* The minimum number of rows we leave space for for the long events in the
   day view. */
#define DAY_VIEW_MIN_ROWS_IN_TOP_DISPLAY	2

/* The row height for long events in the day view. */
#define DAY_VIEW_ROW_HEIGHT		20

/* The minutes per row in the day view printout. */
#define DAY_VIEW_MINS_PER_ROW		30

#define DAY_VIEW_ROWS			((60 / DAY_VIEW_MINS_PER_ROW) * 24)

/* The width of the column with all the times in it. */
#define DAY_VIEW_TIME_COLUMN_WIDTH	36

/* The space on the right of each event. */
#define DAY_VIEW_EVENT_X_PAD		8

/* Allowance for small errors in floating point comparisons. */
#define EPSILON			0.01

/* The weird month of September 1752, where 3 Sep through 13 Sep were
   eliminated due to the Gregorian reformation. */
static const int sept_1752[42] = {
	 0,  0,  1,  2, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30,
	 0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0
};
#define SEPT_1752_START 2		/* Start day within month */
#define SEPT_1752_END 20		/* End day within month */


struct pdinfo
{
	gint days_shown;
	time_t day_starts[E_DAY_VIEW_MAX_DAYS + 1];

	GArray *long_events;
	GArray *events[E_DAY_VIEW_MAX_DAYS];

	gint start_hour;
	gint end_hour;
	gint start_minute_offset;
	gint end_minute_offset;
	gint rows;
	gint mins_per_row;
	gint8 cols_per_row[DAY_VIEW_ROWS];
	gboolean use_24_hour_format;
};

struct psinfo
{
	gint days_shown;
	time_t day_starts[E_WEEK_VIEW_MAX_WEEKS * 7 + 1];

	GArray *events;

	int rows_per_cell;
	int rows_per_compressed_cell;
	int display_start_weekday;
	gboolean multi_week_view;
	int weeks_shown;
	int month;
	gboolean compress_weekend;
	gboolean use_24_hour_format;
	double row_height;
	double header_row_height;
};

struct ptinfo
{
	GList *todos;
};

struct einfo
{
	char *text;
	time_t start;
	time_t end;
	int count;
};

/* Convenience function to help the transition to timezone functions.
   It converts a time_t to a struct tm. */
static struct tm*
convert_timet_to_struct_tm (time_t time, icaltimezone *zone)
{
	static struct tm my_tm;
	struct icaltimetype tt;

	/* Convert it to an icaltimetype. */
	tt = icaltime_from_timet_with_zone (time, FALSE, zone);

	/* Fill in the struct tm. */
	my_tm.tm_year = tt.year - 1900;
	my_tm.tm_mon = tt.month - 1;
	my_tm.tm_mday = tt.day;
	my_tm.tm_hour = tt.hour;
	my_tm.tm_min = tt.minute;
	my_tm.tm_sec = tt.second;
	my_tm.tm_isdst = tt.is_daylight;

	my_tm.tm_wday = time_day_of_week (tt.day, tt.month - 1, tt.year);

	return &my_tm;
}


/* Fills the 42-element days array with the day numbers for the specified month.  Slots outside the
 * bounds of the month are filled with zeros.  The starting and ending indexes of the days are
 * returned in the start and end arguments.
 */
static void
build_month (int month, int year, int *days, int *start, int *end)
{
	int i;
	int d_month, d_week, week_start_day;

	/* Note that months are zero-based, so September is month 8 */

	if ((year == 1752) && (month == 8)) {
		memcpy (days, sept_1752, 42 * sizeof (int));

		if (start)
			*start = SEPT_1752_START;

		if (end)
			*end = SEPT_1752_END;

		return;
	}

	for (i = 0; i < 42; i++)
		days[i] = 0;

	d_month = time_days_in_month (year, month);
	/* Get the start weekday in the month, 0=Sun to 6=Sat. */
	d_week = time_day_of_week (1, month, year);

	/* Get the configuration setting specifying which weekday we put on
	   the left column, 0=Sun to 6=Sat. */
	week_start_day = calendar_config_get_week_start_day ();

	/* Figure out which square we want to put the 1 in. */
	d_week = (d_week + 7 - week_start_day) % 7;

	for (i = 0; i < d_month; i++)
		days[d_week + i] = i + 1;

	if (start)
		*start = d_week;

	if (end)
		*end = d_week + d_month - 1;
}

static PangoFontDescription *
get_font_for_size (EvoCalendarPrintRenderer *pr, double h, GnomeFontWeight weight, gboolean italic)
{
	PangoFontDescription *font_desc;
	gchar *font_name;

	if (weight <= GNOME_FONT_BOOK)
		font_name = REGULAR_FONT;
	else
		font_name = BOLD_FONT;

	if (italic)
		font_name = g_strconcat (font_name, " Italic", NULL);

	font_desc = pango_font_description_from_string (font_name);

	#define MAGIC_SCALE_FACTOR (0.86)
	pango_font_description_set_size (font_desc,
					 h * MAGIC_SCALE_FACTOR * PANGO_SCALE );

	if (italic)
		g_free (font_name);

	return font_desc;
}

enum align_box {
	ALIGN_LEFT=1,
	ALIGN_RIGHT,
	ALIGN_CENTER,
	ALIGN_BORDER= 1<<8
};


/* Prints a rectangle, with or without a border, filled or outline, and
   possibly with triangular arrows at one or both horizontal edges.
   width      = width of border, -ve means no border.
   red,green,blue = bgcolor to fill,   -ve means no fill.
   left_triangle_width, right_triangle_width = width from edge of rectangle to
          point of triangle, or -ve for no triangle. */
static void
print_border_with_triangles (GtkPrintContext *pc,
			     double l, double r, double t, double b,
			     double width, double red, double green, double blue,
			     double left_triangle_width,
			     double right_triangle_width)
{
	cairo_t *cr;

	cr = gtk_print_context_get_cairo_context (pc);
	cairo_save (cr);

	/* Fill in the interior of the rectangle, if desired. */
	if (red >= -EPSILON && green >= -EPSILON && blue >= -EPSILON) {
		
		cairo_move_to (cr, l, t);
		
		if (left_triangle_width > 0.0)
			cairo_line_to (cr, l - left_triangle_width,
				      (t + b) / 2);

		cairo_line_to (cr, l, b);
		cairo_line_to (cr, r, b);
		
		if (right_triangle_width > 0.0)
			cairo_line_to (cr, r + right_triangle_width, (t + b) / 2);

		cairo_line_to (cr, r, t);
		cairo_close_path (cr);
		cairo_set_source_rgb (cr, red, green, blue);
		cairo_fill (cr);
		cairo_restore (cr);
		cairo_save (cr);
	}

	/* Draw the outline, if desired. */
	if (width >= -EPSILON) {

		cr = gtk_print_context_get_cairo_context (pc);
		cairo_move_to (cr, l, t);

		if (left_triangle_width > 0.0)
			cairo_line_to (cr, l - left_triangle_width,
					    (t + b) / 2);

		cairo_line_to (cr, l, b);
		cairo_line_to (cr, r, b);

		if (right_triangle_width > 0.0)
			cairo_line_to (cr, r + right_triangle_width,
					    (t + b) / 2);

		cairo_line_to (cr, r, t);
		cairo_close_path (cr);
		cairo_set_source_rgb (cr, 0, 0, 0);
		cairo_set_line_width (cr, width);
		cairo_stroke (cr);	
		
	}

	cairo_restore (cr);
}


/* Prints a rectangle, with or without a border, and filled or outline.
   width      = width of border, -ve means no border.
   fillcolor = shade of fill,   -ve means no fill. */
static void
print_border_rgb (GtkPrintContext *pc,
	      double l, double r, double t, double b,
	      double width, double red, double green, double blue)
{
	print_border_with_triangles (pc, l, r, t, b, width, red, green, blue, -1.0, -1.0);
}

static void
print_border (GtkPrintContext *pc,
	      double l, double r, double t, double b,
	      double width, double fillcolor)
{
	print_border_rgb (pc, l, r, t, b, width, fillcolor, fillcolor, fillcolor);
}

static void
print_rectangle (GtkPrintContext *pc,
		 double l, double r, double t, double b,
		 double red, double green, double blue)
{
	cairo_t *cr;
	
	cr = gtk_print_context_get_cairo_context (pc);
	cairo_save (cr);

	cairo_move_to (cr, l, t);
	cairo_line_to (cr, l, b);
	cairo_line_to (cr, r, b);
	cairo_line_to (cr, r, t);
	cairo_close_path (cr);
	cairo_set_source_rgb (cr, red, green, blue);
	cairo_fill (cr);

	cairo_restore (cr);
}

/* Prints 1 line of aligned text in a box. It is centered vertically, and
   the horizontal alignment can be either ALIGN_LEFT, ALIGN_RIGHT, or
   ALIGN_CENTER. */
static double
print_text (EvoCalendarPrintRenderer *pr, PangoFontDescription *font, const char *text,
	   enum align_box align, double l, double r, double t, double b)
{
	double w, x;
	int pw, ph;
	cairo_t *cr;

	cr = gtk_print_context_get_cairo_context (pr->pc);
	cairo_save (cr);

	pango_layout_set_font_description (pr->pl, font);
	pango_layout_set_text (pr->pl, text, -1);
	pango_layout_set_width (pr->pl, -1); /* ensure no line-wrapping occurs */
	pango_layout_set_indent (pr->pl, 0);
	pango_layout_get_size (pr->pl, &pw, &ph);
	w = pw/(double)PANGO_SCALE;

	switch (align & 3) {
	case ALIGN_LEFT:
	default:
		x = l;
		break;
	case ALIGN_RIGHT:
		x = r - w;
		break;
	case ALIGN_CENTER:
		x = l + ((r - l) - w) / 2;
		break;
	}

	/* Make sure we don't go off the left edge. */
	x = MAX (l, x);

	/* Set a clipping rectangle. */
	cairo_move_to (cr, l, t);	
	cairo_rectangle (cr, l, t, r, b);
	cairo_clip (cr);

	cairo_new_path (cr);
	cairo_set_source_rgb (cr, 0, 0, 0);

	cairo_move_to (cr, x, t);
	pango_cairo_show_layout (cr, pr->pl);

	cairo_stroke(cr);
	cairo_restore (cr);

	return w;
}

/* gets/frees the font for you, as a book font */
static double
print_text_size(EvoCalendarPrintRenderer *pr, const char *text,
		     enum align_box align, double l, double r, double t, double b)
{
	PangoFontDescription *font;
	double w;
	
	if(t > b)
	font = get_font_for_size (pr,t - b, GNOME_FONT_BOOK, FALSE);
	else
		{
	font = get_font_for_size (pr, b - t , GNOME_FONT_BOOK, FALSE);	
	}
	w = print_text(pr, font, text, align, l, r, t, b);
	pango_font_description_free (font);

	return w;
}

/* gets/frees the font for you, as a bold font */
static double
print_text_size_bold(EvoCalendarPrintRenderer *pr, const char *text,
		     enum align_box align, double l, double r, double t, double b)
{
	PangoFontDescription *font;
	double w;

	if (t > b)	
		font = get_font_for_size (pr, t - b , GNOME_FONT_BOLD, FALSE);
	else
		font = get_font_for_size (pr, b - t, GNOME_FONT_BOLD, FALSE);

	w = print_text(pr, font, text, align, l, r, t, b);
	pango_font_description_free (font);

	return w;
}

static void
titled_box (EvoCalendarPrintRenderer *pr, const char *text, PangoFontDescription *font,
	    enum align_box align, double *l, double *r, double *t, double *b,
	    double linewidth)
{
	double size;

	size = get_font_size (font);
	if (align & ALIGN_BORDER) {
		print_border (pr->pc, *l, *r, *t, *t + size * 1.4,
			      linewidth, 0.9);
		print_border (pr->pc, *l, *r, *t + size * 1.4, *b,
			      linewidth, -1.0);
		*l += 2;
		*r -= 2;
		*b += 2;
	}
	print_text (pr, font, text, align, *l, *r, *t, *t + size * 1.4);
	*t += size * 1.4;
}

enum datefmt {
	DATE_MONTH	= 1 << 0,
	DATE_DAY	= 1 << 1,
	DATE_DAYNAME	= 1 << 2,
	DATE_YEAR	= 1 << 3
};

static char *days[] = {
	N_("1st"), N_("2nd"), N_("3rd"), N_("4th"), N_("5th"),
	N_("6th"), N_("7th"), N_("8th"), N_("9th"), N_("10th"),
	N_("11th"), N_("12th"), N_("13th"), N_("14th"), N_("15th"),
	N_("16th"), N_("17th"), N_("18th"), N_("19th"), N_("20th"),
	N_("21st"), N_("22nd"), N_("23rd"), N_("24th"), N_("25th"),
	N_("26th"), N_("27th"), N_("28th"), N_("29th"),	N_("30th"),
	N_("31st")
};

/*
  format the date 'nicely' and consistently for various headers
*/
static char *
format_date(time_t time, int flags, char *buffer, int bufflen)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	char fmt[64];
	struct tm tm;

	tm = *convert_timet_to_struct_tm (time, zone);

	fmt[0] = 0;
	if (flags & DATE_DAYNAME) {
		strcat(fmt, "%A");
	}
	if (flags & DATE_DAY) {
		if (flags & DATE_DAYNAME)
			strcat(fmt, " ");
		strcat(fmt, gettext(days[tm.tm_mday-1]));
	}
	if (flags & DATE_MONTH) {
		if (flags & (DATE_DAY|DATE_DAYNAME))
			strcat(fmt, " ");
		strcat(fmt, "%B");
		if ((flags & (DATE_DAY|DATE_YEAR)) == (DATE_DAY|DATE_YEAR))
			strcat(fmt, ",");
	}
	if (flags & DATE_YEAR) {
		if (flags & (DATE_DAY|DATE_DAYNAME|DATE_MONTH))
			strcat(fmt, " ");
		strcat(fmt, "%Y");
	}
	e_utf8_strftime(buffer, bufflen, fmt, &tm);
	buffer[bufflen - 1] = '\0';

	return buffer;
}

static gboolean 
instance_cb (ECalComponent *comp, time_t instance_start, time_t instance_end, gpointer data)
{
	
	gboolean *found = ((ECalModelGenerateInstancesData *) data)->cb_data;
	
	*found = TRUE;
	
	return FALSE;
}

/*
  print out the month small, embolden any days with events.
*/
static void
print_month_small (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t month,
		   double left, double right, double top, double bottom,
		   int titleflags, time_t greystart, time_t greyend,
		   int bordertitle)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	PangoFontDescription *font, *font_bold, *font_normal;
	time_t now, next;
	int x, y;
	int days[42];
	int day, weekday, week_start_day;
	char buf[100];
	struct tm tm;
	double font_size, max_font_size;
	double header_size, col_width, row_height, text_xpad, w;
	double cell_top, cell_bottom, cell_left, cell_right, text_right;

	/* Translators: These are workday abbreviations, e.g. Su=Sunday and Th=Thursday */
	char *daynames[] = { N_("Su"), N_("Mo"), N_("Tu"), N_("We"),
			     N_("Th"), N_("Fr"), N_("Sa") };
	cairo_t *cr;

	/* Print the title, e.g. 'June 2001', in the top 16% of the area. */
	format_date (month, titleflags, buf, 100);

	if(top < bottom)
		header_size = (bottom - top) * 0.16;
	else
		header_size = (top - bottom) * 0.16;

	font = get_font_for_size (pr, header_size, GNOME_FONT_BOLD, FALSE);
	if (bordertitle) {
		print_border (pr->pc, left, right, top, top + header_size,
			      1.0, 0.9);
	}
	print_text (pr, font, buf, ALIGN_CENTER, left, right,
		    top, top + header_size);
	pango_font_description_free (font);

	top += header_size;
	col_width = (right - left) / 7;

	/* The top row with the day abbreviations gets an extra bit of
	   vertical space around it. */
	if (bottom > top )
		row_height = (bottom - top) / 7.4; 
	else
		row_height = (top - bottom ) / 7.4;

	/* First we need to calculate a reasonable font size. We start with a
	   rough guess of just under the height of each row. */
	font_size = row_height;

	/* Check that it isn't going to be too wide. The characters are about
	   twice as high as they are wide, but we need to fit two characters
	   into each cell, so we don't want to go over col_width. */
	max_font_size = col_width * 0.65;

	/* Why calculate this if we're not using it? */
	font_size = row_height;

	/* get month days */
	tm = *convert_timet_to_struct_tm (month, zone);
	build_month (tm.tm_mon, tm.tm_year + 1900, days, 0, 0);

	font_normal = get_font_for_size (pr, font_size, GNOME_FONT_BOOK, FALSE);
	font_bold = get_font_for_size (pr, font_size, GNOME_FONT_BOLD, FALSE);

	/* Get a reasonable estimate of the largest number we will need,
	   and use it to calculate the offset from the right edge of the
	   cell that we should put the numbers. */
	w = evo_calendar_print_renderer_get_width (pr, font_bold, "23");
	text_xpad = (col_width - w) / 2;

	cr = gtk_print_context_get_cairo_context (pr->pc);
	cairo_set_source_rgb (cr, 0, 0, 0);

	/* Print the abbreviated day names across the top in bold. */
	week_start_day = calendar_config_get_week_start_day ();
	weekday = week_start_day;
	for (x = 0; x < 7; x++) {
		print_text (pr, font_bold, _(daynames[weekday]), ALIGN_CENTER,
			    left + x * col_width, left + (x + 1) * col_width,
			    top, top + row_height * 1.4);
		weekday = (weekday + 1) % 7;
	}

	top += row_height * 1.4;

	now = time_month_begin_with_zone (month, zone);
	for (y = 0; y < 6; y++) {

		cell_top = top + y * row_height;
		cell_bottom = cell_top + row_height;

		for (x = 0; x < 7; x++) {

			cell_left = left + x * col_width;
			/* We add a 0.05 to make sure the cells meet up with
			   each other. Otherwise you sometimes get lines
			   between them which looks bad. Maybe I'm not using
			   coords in the way gnome-print expects. */
			cell_right = cell_left + col_width + 0.05;
			text_right = cell_right - text_xpad;

			day = days[y * 7 + x];
			if (day != 0) {
				gboolean found = FALSE;
				sprintf (buf, "%d", day);

				/* this is a slow messy way to do this ... but easy ... */
				e_cal_model_generate_instances (gnome_calendar_get_calendar_model (gcal), now, 
								time_day_end_with_zone (now, zone),
								instance_cb, &found);
				
				font = found ? font_bold : font_normal;

				next = time_add_day_with_zone (now, 1, zone);
				if ((now >= greystart && now < greyend) 
				    || (greystart >= now && greystart < next)) {
					print_border (pr->pc,
						      cell_left, cell_right,
						      cell_top, cell_bottom,
						      -1.0, 0.75); 
				}
				print_text (pr, font, buf, ALIGN_RIGHT,
					    cell_left, text_right,
					    cell_top, cell_bottom);

				now = next;
			}
		}
	}
	pango_font_description_free (font_normal);
	pango_font_description_free (font_bold);
}


/* wraps text into the print context, not taking up more than its allowed space */
static double
bound_text (EvoCalendarPrintRenderer *pr, PangoFontDescription *font, const char *text,
	   double left, double right, double top, double bottom, double indent)
{
	/* Let Pango do all the work: */
	int w=0, h=0;
	cairo_t *cr;
	
	cr = gtk_print_context_get_cairo_context (pr->pc);
	cairo_save (cr);

	/* Set a clipping rectangle. */
	cairo_move_to (cr, left, top);
	cairo_rectangle (cr, left, top, right, bottom);
	cairo_clip (cr);   
	cairo_new_path (cr); 

	pango_layout_set_font_description (pr->pl, font);
	pango_layout_set_text (pr->pl, text, -1);
	pango_layout_set_indent (pr->pl, indent * PANGO_SCALE);
	pango_layout_set_width (pr->pl, (right-left)*PANGO_SCALE);

	cairo_move_to (cr, left, top);
	pango_cairo_show_layout (cr, pr->pl);
	cairo_stroke (cr);

	cairo_restore (cr);
	pango_layout_get_size (pr->pl, &w, &h);
	return top + ((double)h/(double)PANGO_SCALE);
}


/* Draw the borders, lines, and times down the left of the day view. */
static void
print_day_background (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal,
		      time_t whence, struct pdinfo *pdi,
		      double left, double right, double top, double bottom)
{
	PangoFontDescription *font_hour, *font_minute;
	double yinc, y;
	double width = DAY_VIEW_TIME_COLUMN_WIDTH;
	double font_size, max_font_size, hour_font_size, minute_font_size;
	char buf[20];
	const char *minute;
	gboolean use_24_hour;
	int i, hour, row;
	double hour_minute_x;
	cairo_t *cr;
	
	/* Fill the time column in light-gray. */
	print_border (pr->pc, left, left + width, top, bottom, -1.0, 0.9);

	/* Draw the border around the entire view. */
	cr = gtk_print_context_get_cairo_context (pr->pc);

	cairo_set_source_rgb (cr, 0, 0, 0);
	print_border (pr->pc, left, right, top, bottom, 1.0, -1.0);   

	/* Draw the vertical line on the right of the time column. */
	cr = gtk_print_context_get_cairo_context (pr->pc);
	cairo_set_line_width (cr, 0.0);
	cairo_move_to (cr, left + width, bottom);
	cairo_line_to (cr, left + width, top);
	cairo_stroke (cr);
	
	/* Calculate the row height. */
	if (top > bottom)
		yinc = (top - bottom) / (pdi->end_hour - pdi->start_hour);
	else
		yinc = (bottom - top) / (pdi->end_hour - pdi->start_hour);

        /* Get the 2 fonts we need. */
	font_size = yinc * 0.6;
	max_font_size = width * 0.5;
	hour_font_size = MIN (font_size, max_font_size);
	font_hour = get_font_for_size (pr, hour_font_size, GNOME_FONT_BOLD, FALSE);

	font_size = yinc * 0.33;
	max_font_size = width * 0.25;
	minute_font_size = MIN (font_size, max_font_size);
	font_minute = get_font_for_size (pr, minute_font_size, GNOME_FONT_BOLD, FALSE);

	use_24_hour = calendar_config_get_24_hour_format ();

	row = 0;
	hour_minute_x = left + width * 0.58;
	for (i = pdi->start_hour; i < pdi->end_hour; i++) {
		y = top + yinc * (row + 1) ; 
		cr = gtk_print_context_get_cairo_context (pr->pc);
		cairo_set_source_rgb (cr, 0, 0, 0);

		if (use_24_hour) {
			hour = i;
			minute = "00";
		} else {
			if (i < 12)
				minute = _("am");
			else
				minute = _("pm");

			hour = i % 12;
			if (hour == 0)
				hour = 12;
		}

		/* the hour label/minute */
		sprintf (buf, "%d", hour);
		print_text (pr, font_hour, buf, ALIGN_RIGHT,
			    left, hour_minute_x,
			    y - yinc + yinc / 2, y - yinc + yinc / 2 + hour_font_size);
		print_text (pr, font_minute, minute, ALIGN_LEFT,
			    hour_minute_x, left + width - 3,
			    y - yinc + yinc / 2, y - yinc + yinc / 2 + minute_font_size);
		
                /* Draw the horizontal line between hours, across the entire
		   width of the day view. */
		cr = gtk_print_context_get_cairo_context (pr->pc);
		cairo_move_to (cr, left, y);
		cairo_line_to (cr, right, y);
		cairo_set_line_width (cr, 1);
		cairo_stroke (cr);
			
		/* Draw the horizontal line for the 1/2-hours, across the
		   entire width except for part of the time column. */
		cairo_move_to (cr, left + width * 0.6, y - yinc / 2);
		cairo_line_to (cr, right, y - yinc / 2);
		cairo_set_line_width (cr, 1);
		cairo_stroke (cr);
		row ++;
	}

	pango_font_description_free (font_hour);
	pango_font_description_free (font_minute);
}


/* This adds one event to the view, adding it to the appropriate array. */
static gint
print_day_add_event (ECalModelComponent *comp_data,
		     time_t	    start,
		     time_t	    end,
		     gint	    days_shown,
		     time_t	   *day_starts,
		     GArray	   *long_events,
		     GArray	  **events)

{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	EDayViewEvent event;
	gint day, offset;
	struct icaltimetype start_tt, end_tt;

#if 0
	g_print ("Day view lower: %s", ctime (&day_starts[0]));
	g_print ("Day view upper: %s", ctime (&day_starts[days_shown]));
	g_print ("Event start: %s", ctime (&start));
	g_print ("Event end  : %s\n", ctime (&end));
#endif

	/* Check that the event times are valid. */
	g_return_val_if_fail (start <= end, -1);
	g_return_val_if_fail (start < day_starts[days_shown], -1);
	g_return_val_if_fail (end > day_starts[0], -1);

	start_tt = icaltime_from_timet_with_zone (start, FALSE, zone);
	end_tt = icaltime_from_timet_with_zone (end, FALSE, zone);

	event.comp_data = comp_data;
	event.start = start;
	event.end = end;
	event.canvas_item = NULL;

	/* Calculate the start & end minute, relative to the top of the
	   display. */
	/*offset = day_view->first_hour_shown * 60
	  + day_view->first_minute_shown;*/
	offset = 0;
	event.start_minute = start_tt.hour * 60 + start_tt.minute - offset;
	event.end_minute = end_tt.hour * 60 + end_tt.minute - offset;

	event.start_row_or_col = 0;
	event.num_columns = 0;

	/* Find out which array to add the event to. */
	for (day = 0; day < days_shown; day++) {
		if (start >= day_starts[day] && end <= day_starts[day + 1]) {

			/* Special case for when the appointment ends at
			   midnight, i.e. the start of the next day. */
			if (end == day_starts[day + 1]) {

				/* If the event last the entire day, then we
				   skip it here so it gets added to the top
				   canvas. */
				if (start == day_starts[day])
				    break;

				event.end_minute = 24 * 60;
			}
			g_array_append_val (events[day], event);
			return day;
		}
	}

	/* The event wasn't within one day so it must be a long event,
	   i.e. shown in the top canvas. */
	g_array_append_val (long_events, event);
	return E_DAY_VIEW_LONG_EVENT;
}


static gboolean
print_day_details_cb (ECalComponent *comp, time_t istart, time_t iend,
		      gpointer data)
{
	ECalModelGenerateInstancesData *mdata = (ECalModelGenerateInstancesData *) data;
	struct pdinfo *pdi = (struct pdinfo *) mdata->cb_data;

	print_day_add_event (mdata->comp_data, istart, iend,
			     pdi->days_shown, pdi->day_starts,
			     pdi->long_events, pdi->events);

	return TRUE;
}


static void
free_event_array (GArray *array)
{
	EDayViewEvent *event;
	gint event_num;

	for (event_num = 0; event_num < array->len; event_num++) {
		event = &g_array_index (array, EDayViewEvent, event_num);
		if (event->canvas_item)
			gtk_object_destroy (GTK_OBJECT (event->canvas_item));
	}

	g_array_set_size (array, 0);
}


static void
print_day_long_event (EvoCalendarPrintRenderer *pr, PangoFontDescription *font,
		      double left, double right, double top, double bottom,
		      double row_height, EDayViewEvent *event,
		      struct pdinfo *pdi, ECalModel *model)
{
	const gchar *summary;
	double x1, x2, y1, y2;
	double left_triangle_width = -1.0, right_triangle_width = -1.0;
	char *text;
	char buffer[32];
	struct tm date_tm;
	double red, green, blue;

	/* If the event starts before the first day being printed, draw a
	   triangle. (Note that I am assuming we are just showing 1 day at
	   the moment.) */
	if (event->start < pdi->day_starts[0])
		left_triangle_width = 4;

	/* If the event ends after the last day being printed, draw a
	   triangle. */
	if (event->end > pdi->day_starts[1])
		right_triangle_width = 4;

	x1 = left + 10;
	x2 = right - 10;
	y1 = top + event->start_row_or_col * row_height + 4;
	y2 = y1 + row_height-7;
	red = green = blue = 0.95;
	e_cal_model_get_rgb_color_for_component (model, event->comp_data, &red, &green, &blue);
	print_border_with_triangles (pr->pc, x1, x2, y1, y2, 0.5, red, green, blue,
				     left_triangle_width,
				     right_triangle_width);

	/* If the event starts after the first day being printed, we need to
	   print the start time. */
	if (event->start > pdi->day_starts[0]) {
		date_tm.tm_year = 2001;
		date_tm.tm_mon = 0;
		date_tm.tm_mday = 1;
		date_tm.tm_hour = event->start_minute / 60;
		date_tm.tm_min = event->start_minute % 60;
		date_tm.tm_sec = 0;
		date_tm.tm_isdst = -1;

		e_time_format_time (&date_tm, pdi->use_24_hour_format, FALSE,
				    buffer, sizeof (buffer));

		x1 += 4;
		x1 += print_text (pr, font, buffer, ALIGN_LEFT, x1, x2, y1, y2);
	}

	/* If the event ends before the end of the last day being printed,
	   we need to print the end time. */
	if (event->end < pdi->day_starts[1]) {
		date_tm.tm_year = 2001;
		date_tm.tm_mon = 0;
		date_tm.tm_mday = 1;
		date_tm.tm_hour = event->end_minute / 60;
		date_tm.tm_min = event->end_minute % 60;
		date_tm.tm_sec = 0;
		date_tm.tm_isdst = -1;

		e_time_format_time (&date_tm, pdi->use_24_hour_format, FALSE,
				    buffer, sizeof (buffer));

		x2 -= 4;
		x2 -= print_text (pr, font, buffer, ALIGN_RIGHT, x1, x2, y1, y2);
	}

	/* Print the text. */
	summary = icalcomponent_get_summary (event->comp_data->icalcomp);
	text = summary ? (char*) summary : "";

	x1 += 4;
	x2 -= 4;
	print_text (pr, font, text, ALIGN_CENTER, x1, x2, y1, y2);
}


static void
print_day_event (EvoCalendarPrintRenderer *pr, PangoFontDescription *font,
		 double left, double right, double top, double bottom,
		 EDayViewEvent *event, struct pdinfo *pdi, ECalModel *model)
{
	const gchar *summary;
	double x1, x2, y1, y2, col_width, row_height;
	int start_offset, end_offset, start_row, end_row;
	char *text, start_buffer[32], end_buffer[32];
	gboolean display_times = FALSE, free_text = FALSE;
	struct tm date_tm;
	double red, green, blue;

	if ((event->start_minute >= pdi->end_minute_offset)
	    || (event->end_minute <= pdi->start_minute_offset))
		return;

	start_offset = event->start_minute - pdi->start_minute_offset;
	end_offset = event->end_minute - pdi->start_minute_offset;

	start_row = start_offset / pdi->mins_per_row;
	start_row = MAX (0, start_row);
	end_row = (end_offset - 1) / pdi->mins_per_row;
	end_row = MIN (pdi->rows - 1, end_row);
	col_width = (right - left) / pdi->cols_per_row[event->start_minute / pdi->mins_per_row];

	if (start_offset != start_row * pdi->mins_per_row
	    || end_offset != (end_row + 1) * pdi->mins_per_row)
		display_times = TRUE;

	x1 = left + event->start_row_or_col * col_width;
	x2 = x1 + event->num_columns * col_width - DAY_VIEW_EVENT_X_PAD;

	row_height = (bottom - top) / pdi->rows;
	y1 = top + start_row * row_height;
	y2 = top + (end_row + 1) * row_height;
#if 0 
	g_print ("Event: %g,%g %g,%g\n  row_height: %g start_row: %i top: %g rows: %i\n",
		 x1, y1, x2, y2, row_height, start_row, top, pdi->rows);
#endif

	red = green = blue = 0.95;
	e_cal_model_get_rgb_color_for_component (model, event->comp_data, &red, &green, &blue);
	print_border_rgb (pr->pc, x1, x2, y1, y2, 1.0, red, green, blue);

	summary = icalcomponent_get_summary (event->comp_data->icalcomp);
	text = summary ? (char*) summary : "";
	

	if (display_times) {
		date_tm.tm_year = 2001;
		date_tm.tm_mon = 0;
		date_tm.tm_mday = 1;
		date_tm.tm_hour = event->start_minute / 60;
		date_tm.tm_min = event->start_minute % 60;
		date_tm.tm_sec = 0;
		date_tm.tm_isdst = -1;

		e_time_format_time (&date_tm, pdi->use_24_hour_format, FALSE,
				    start_buffer, sizeof (start_buffer));

		date_tm.tm_hour = event->end_minute / 60;
		date_tm.tm_min = event->end_minute % 60;

		e_time_format_time (&date_tm, pdi->use_24_hour_format, FALSE,
				    end_buffer, sizeof (end_buffer));

		text = g_strdup_printf ("%s - %s %s ", start_buffer,
					end_buffer, text);

		free_text = TRUE;
	}

	bound_text (pr, font, text, x1 + 2, x2 - 2, y1, y2, 0);

	if (free_text)
		g_free (text);
}


static void
print_day_details (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t whence,
		   double left, double right, double top, double bottom)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	EDayViewEvent *event;
	PangoFontDescription *font;
	time_t start, end;
	struct pdinfo pdi;
	gint rows_in_top_display, i;
	double font_size, max_font_size;
	cairo_t *cr;

	ECalModel *model = gnome_calendar_get_calendar_model (gcal);

	start = time_day_begin_with_zone (whence, zone);
	end = time_day_end_with_zone (start, zone);

	pdi.days_shown = 1;
	pdi.day_starts[0] = start;
	pdi.day_starts[1] = end;
	pdi.long_events = g_array_new (FALSE, FALSE, sizeof (EDayViewEvent));
	pdi.events[0] = g_array_new (FALSE, FALSE, sizeof (EDayViewEvent));
	pdi.start_hour = calendar_config_get_day_start_hour ();
	pdi.end_hour = calendar_config_get_day_end_hour ();
	if (calendar_config_get_day_end_minute () != 0)
		pdi.end_hour++;
	pdi.rows = (pdi.end_hour - pdi.start_hour) * 2;
	pdi.mins_per_row = 30;
	pdi.start_minute_offset = pdi.start_hour * 60;
	pdi.end_minute_offset = pdi.end_hour * 60;
	pdi.use_24_hour_format = calendar_config_get_24_hour_format ();

	/* Get the events from the server. */
	e_cal_model_generate_instances (model, start, end, print_day_details_cb, &pdi);
	qsort (pdi.long_events->data, pdi.long_events->len,
	       sizeof (EDayViewEvent), e_day_view_event_sort_func);
	qsort (pdi.events[0]->data, pdi.events[0]->len,
	       sizeof (EDayViewEvent), e_day_view_event_sort_func);

	/* Also print events outside of work hours */
	if (pdi.events[0]->len > 0) {
		struct icaltimetype tt;

		event = &g_array_index (pdi.events[0], EDayViewEvent, 0);		
		tt = icaltime_from_timet_with_zone (event->start, FALSE, zone);
		if (tt.hour < pdi.start_hour)
			pdi.start_hour = tt.hour;
		pdi.start_minute_offset = pdi.start_hour * 60;

		event = &g_array_index (pdi.events[0], EDayViewEvent, pdi.events[0]->len - 1);
		tt = icaltime_from_timet_with_zone (event->end, FALSE, zone);
		if (tt.hour > pdi.end_hour || tt.hour == 0) {
			pdi.end_hour = tt.hour ? tt.hour : 24;
			if (tt.minute > 0)
				pdi.end_hour++;
		}
		pdi.end_minute_offset = pdi.end_hour * 60;

		pdi.rows = (pdi.end_hour - pdi.start_hour) * 2;
	}
	
	/* Lay them out the long events, across the top of the page. */
	e_day_view_layout_long_events (pdi.long_events, pdi.days_shown,
				       pdi.day_starts, &rows_in_top_display);

	 /*Print the long events. */
	font = get_font_for_size (pr, 12, GNOME_FONT_BOOK, FALSE);
	for (i = 0; i < pdi.long_events->len; i++) {
		event = &g_array_index (pdi.long_events, EDayViewEvent, i);
		print_day_long_event (pr, font, left, right, top, bottom,
				      DAY_VIEW_ROW_HEIGHT, event, &pdi, model);
	}
	
	/* We always leave space for DAY_VIEW_MIN_ROWS_IN_TOP_DISPLAY in the
	   top display, but we may have more rows than that, in which case
	   the main display area will be compressed. */
	rows_in_top_display = MAX (rows_in_top_display,
				   DAY_VIEW_MIN_ROWS_IN_TOP_DISPLAY);

	/* Draw the border around the long events. */
	cr = gtk_print_context_get_cairo_context (pr->pc);

	cairo_set_source_rgb (cr, 0, 0, 0);
		print_border (pr->pc, left, right,
		      top, top + rows_in_top_display * DAY_VIEW_ROW_HEIGHT - 4,
		      1.0, -1.0); 
		
	/* Adjust the area containing the main display. */
	top += rows_in_top_display * DAY_VIEW_ROW_HEIGHT - 2;
	
	/* Draw the borders, lines, and times down the left. */
	print_day_background (pr, gcal, whence, &pdi,
			      left, right, top, bottom);
	/* Now adjust to get rid of the time column. */
	left += DAY_VIEW_TIME_COLUMN_WIDTH;
	
	
	/* lay out the short events, within the day. */
	e_day_view_layout_day_events (pdi.events[0], DAY_VIEW_ROWS,
				      DAY_VIEW_MINS_PER_ROW, pdi.cols_per_row);

	/* print the short events. */
	if (top > bottom )
		max_font_size = ((top - bottom) / pdi.rows) - 4;
	else
		max_font_size = ((bottom - top ) / pdi.rows) - 4;
 	font_size = MIN(DAY_NORMAL_FONT_SIZE, max_font_size);
	font = get_font_for_size (pr, font_size, GNOME_FONT_BOOK, FALSE);

	for (i = 0; i < pdi.events[0]->len; i++) {
		event = &g_array_index (pdi.events[0], EDayViewEvent, i);
		print_day_event (pr, font, left, right, top, bottom,
				 event, &pdi, model);
	}
	
	/* Free everything. */
	free_event_array (pdi.long_events);
	pango_font_description_free (font);
	g_array_free (pdi.long_events, TRUE);
	free_event_array (pdi.events[0]);
	g_array_free (pdi.events[0], TRUE);
}

/* Returns TRUE if the event is a one-day event (i.e. not a long event). */
static gboolean
print_is_one_day_week_event (EWeekViewEvent *event,
			     EWeekViewEventSpan *span,
			     time_t *day_starts)
{
	if (event->start == day_starts[span->start_day]
	    && event->end == day_starts[span->start_day + 1])
		return FALSE;

	if (span->num_days == 1
	    && event->start >= day_starts[span->start_day]
	    && event->end <= day_starts[span->start_day + 1])
		return TRUE;

	return FALSE;
}


static void
print_week_long_event (EvoCalendarPrintRenderer *pr, PangoFontDescription *font,
		       struct psinfo *psi,
		       double x1, double x2, double y1, double y2,
		       EWeekViewEvent *event, EWeekViewEventSpan *span,
		       char *text, double red, double green, double blue)
{
	double left_triangle_width = -1.0, right_triangle_width = -1.0;
	struct tm date_tm;
	char buffer[32];

	/* If the event starts before the first day of the span, draw a
	   triangle to indicate it continues. */
	if (event->start < psi->day_starts[span->start_day])
		left_triangle_width = 4;

	/* If the event ends after the last day of the span, draw a
	   triangle. */
	if (event->end > psi->day_starts[span->start_day + span->num_days])
		right_triangle_width = 4;

	 print_border_with_triangles (pr->pc, x1, x2, y1, y2 + 4, 0.5, red, green, blue,
				     left_triangle_width,
				     right_triangle_width);

	/* If the event starts after the first day being printed, we need to
	   print the start time. */
	if (event->start > psi->day_starts[span->start_day]) {
		date_tm.tm_year = 2001;
		date_tm.tm_mon = 0;
		date_tm.tm_mday = 1;
		date_tm.tm_hour = event->start_minute / 60;
		date_tm.tm_min = event->start_minute % 60;
		date_tm.tm_sec = 0;
		date_tm.tm_isdst = -1;

		e_time_format_time (&date_tm, psi->use_24_hour_format, FALSE,
				    buffer, sizeof (buffer));

		x1 += 4;
		x1 += print_text_size (pr, buffer, ALIGN_LEFT, x1, x2, y1, y2);
	}

	/* If the event ends before the end of the last day being printed,
	   we need to print the end time. */
	if (event->end < psi->day_starts[span->start_day + span->num_days]) {
		date_tm.tm_year = 2001;
		date_tm.tm_mon = 0;
		date_tm.tm_mday = 1;
		date_tm.tm_hour = event->end_minute / 60;
		date_tm.tm_min = event->end_minute % 60;
		date_tm.tm_sec = 0;
		date_tm.tm_isdst = -1;

		e_time_format_time (&date_tm, psi->use_24_hour_format, FALSE,
				    buffer, sizeof (buffer));

		x2 -= 4;
		x2 -= print_text_size (pr, buffer, ALIGN_RIGHT, x1, x2, y1, y2);
	}

	x1 += 4;
	x2 -= 4;
	print_text_size (pr, text, ALIGN_CENTER, x1, x2, y1, y2);
}


static void
print_week_day_event (EvoCalendarPrintRenderer *pr, PangoFontDescription *font,
		      struct psinfo *psi,
		      double x1, double x2, double y1, double y2,
		      EWeekViewEvent *event, EWeekViewEventSpan *span,
		      char *text, double red, double green, double blue)
{
	struct tm date_tm;
	char buffer[32];

	date_tm.tm_year = 2001;
	date_tm.tm_mon = 0;
	date_tm.tm_mday = 1;
	date_tm.tm_hour = event->start_minute / 60;
	date_tm.tm_min = event->start_minute % 60;
	date_tm.tm_sec = 0;
	date_tm.tm_isdst = -1;

	e_time_format_time (&date_tm, psi->use_24_hour_format, FALSE,
			    buffer, sizeof (buffer));
	print_rectangle (pr->pc, x1, x2 + 6, y1, y2 + 4, red, green, blue);
	x1 += print_text_size (pr, buffer, ALIGN_LEFT, x1, x2, y1, y2 + 3 ) + 4;
	print_text_size (pr, text, ALIGN_LEFT, x1, x2, y1, y2 + 3);    
		
	date_tm.tm_hour = event->end_minute / 60;
	date_tm.tm_min = event->end_minute % 60;

	e_time_format_time (&date_tm, psi->use_24_hour_format, FALSE,
			    buffer, sizeof (buffer));

	print_rectangle (pr->pc, x1, x2 + 6, y1, y2 + 4, red, green, blue); 
	x1 += print_text_size (pr, buffer, ALIGN_LEFT, x1, x2, y1, y2 + 3) + 4;
	print_text_size (pr, text, ALIGN_LEFT, x1, x2, y1, y2 + 3);  
}


static void
print_week_event (EvoCalendarPrintRenderer *pr, PangoFontDescription *font,
		  struct psinfo *psi,
		  double left, double top,
		  double cell_width, double cell_height,
		  ECalModel *model,
		  EWeekViewEvent *event, GArray *spans)
{
	EWeekViewEventSpan *span;
	gint span_num;
	const gchar *summary;
	char *text;
	int num_days, start_x, start_y, start_h, end_x, end_y, end_h;
	double x1, x2, y1, y2;
	double red, green, blue;

	summary = icalcomponent_get_summary (event->comp_data->icalcomp);
	text = summary ? (char*) summary : "";

	for (span_num = 0; span_num < event->num_spans; span_num++) {
		span = &g_array_index (spans, EWeekViewEventSpan,
				       event->spans_index + span_num);

		if (e_week_view_layout_get_span_position
		    (event, span,
		     psi->rows_per_cell,
		     psi->rows_per_compressed_cell,
		     psi->display_start_weekday,
		     psi->multi_week_view,
		     psi->compress_weekend,
		     &num_days)) {

			e_week_view_layout_get_day_position
				(span->start_day,
				 psi->multi_week_view,
				 psi->weeks_shown,
				 psi->display_start_weekday,
				 psi->compress_weekend,
				 &start_x, &start_y, &start_h);

			if (num_days == 1) {
				end_x = start_x;
				end_y = start_y;
				end_h = start_h;
			} else {
				e_week_view_layout_get_day_position
					(span->start_day + num_days - 1,
					 psi->multi_week_view,
					 psi->weeks_shown,
					 psi->display_start_weekday,
					 psi->compress_weekend,
					 &end_x, &end_y, &end_h);
			}

			x1 = left + start_x * cell_width + 6;
			x2 = left + (end_x + 1) * cell_width - 16;
			y1 = top + start_y * cell_height
				 + psi->header_row_height
				 + span->row * psi->row_height;
			y2 = y1 + psi->row_height * 0.5;


			red = .9;
			green = .9;
			blue = .9;
			e_cal_model_get_rgb_color_for_component (model, event->comp_data, &red, &green, &blue);
			if (print_is_one_day_week_event (event, span,
							 psi->day_starts)) {
				print_week_day_event (pr, font, psi,
						      x1, x2, y1, y2,
						      event, span, text, red, green, blue); 
			} else {
				print_week_long_event (pr, font, psi,
						       x1, x2, y1, y2,
						       event, span, text, red, green, blue);
			} 
		}
	}
}


static void
print_week_view_background (EvoCalendarPrintRenderer *pr, PangoFontDescription *font,
			    struct psinfo *psi,
			    double left, double top,
			    double cell_width, double cell_height)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	struct tm tm;
	int day, day_x, day_y, day_h;
	double x1, x2, y1, y2, font_size, fillcolor;
	char *format_string, buffer[128];
	cairo_t *cr;

	font_size = get_font_size (font);

	for (day = 0; day < psi->days_shown; day++) {
		e_week_view_layout_get_day_position
			(day, psi->multi_week_view, psi->weeks_shown,
			 psi->display_start_weekday, psi->compress_weekend,
			 &day_x, &day_y, &day_h);

		x1 = left + day_x * cell_width;
		x2 = left + (day_x + 1) * cell_width;
		y1 = top + day_y * cell_height;
		y2 = y1 + day_h * cell_height;

		tm = *convert_timet_to_struct_tm (psi->day_starts[day], zone);

		/* In the month view we draw a grey background for the end
		   of the previous month and the start of the following. */
		fillcolor = -1.0;
		if (psi->multi_week_view && (tm.tm_mon != psi->month))
			fillcolor = 0.9;

		print_border (pr->pc, x1, x2, y1, y2, 1.0, fillcolor);

		if (psi->multi_week_view) {
			if (tm.tm_mday == 1)
				format_string = _("%d %B");
			else
				format_string = "%d";
		} else {
			cr = gtk_print_context_get_cairo_context (pr->pc);

			cairo_move_to (cr, x1 + 0.1 * cell_width,
					    y1 + psi->header_row_height - 4);
			cairo_line_to (cr, x2,
					    y1 + psi->header_row_height - 4);

			cairo_set_source_rgb (cr, 0, 0, 0);
			cairo_set_line_width (cr, 0.5);
			cairo_stroke (cr);

			/* strftime format %A = full weekday name, %d = day of
			   month, %B = full month name. You can change the
			   order but don't change the specifiers or add
			   anything. */
			format_string = _("%A %d %B");

		}

		e_utf8_strftime (buffer, sizeof (buffer), format_string, &tm);
		
		 print_text_size (pr, buffer, ALIGN_RIGHT,
				 x1, x2 - 4, y1 + 2, y1 + 2 + font_size); 
	}
}

/* This adds one event to the view, adding it to the appropriate array. */
static gboolean
print_week_summary_cb (ECalComponent *comp,
		       time_t	  start,
		       time_t	  end,
		       gpointer	  data)

{
 	icaltimezone *zone = calendar_config_get_icaltimezone ();
 	EWeekViewEvent event;
 	struct icaltimetype start_tt, end_tt;
	ECalModelGenerateInstancesData *mdata = (ECalModelGenerateInstancesData *) data;
 	struct psinfo *psi = (struct psinfo *) mdata->cb_data;

 	/* Check that the event times are valid. */

#if 0
	g_print ("View start:%li end:%li  Event start:%li end:%li\n",
		 psi->day_starts[0], psi->day_starts[psi->days_shown],
		 start, end);
#endif

 	g_return_val_if_fail (start <= end, TRUE);
 	g_return_val_if_fail (start < psi->day_starts[psi->days_shown], TRUE);
 	g_return_val_if_fail (end > psi->day_starts[0], TRUE);

 	start_tt = icaltime_from_timet_with_zone (start, FALSE, zone);
 	end_tt = icaltime_from_timet_with_zone (end, FALSE, zone);

	event.comp_data = e_cal_model_copy_component_data (mdata->comp_data);

	event.start = start;
	event.end = end;
	event.spans_index = 0;
	event.num_spans = 0;

	event.start_minute = start_tt.hour * 60 + start_tt.minute;
	event.end_minute = end_tt.hour * 60 + end_tt.minute;
	if (event.end_minute == 0 && start != end)
		event.end_minute = 24 * 60;

	g_array_append_val (psi->events, event);

	return TRUE;
}

static void
print_week_summary (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal,
		    time_t whence, gboolean multi_week_view, int weeks_shown,
		    int month, double font_size,
		    double left, double right, double top, double bottom)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	EWeekViewEvent *event;
	struct psinfo psi;
	time_t day_start;
	gint rows_per_day[E_WEEK_VIEW_MAX_WEEKS * 7], day, event_num;
	GArray *spans;
	PangoFontDescription *font;
	double cell_width, cell_height;
	ECalModel *model = gnome_calendar_get_calendar_model (gcal);

	psi.days_shown = weeks_shown * 7;
	psi.events = g_array_new (FALSE, FALSE, sizeof (EWeekViewEvent));
	psi.multi_week_view = multi_week_view;
	psi.weeks_shown = weeks_shown;
	psi.month = month;

	/* Get a few config settings. */
	if (multi_week_view)
		psi.compress_weekend = calendar_config_get_compress_weekend ();
	else
		psi.compress_weekend = TRUE;
	psi.use_24_hour_format = calendar_config_get_24_hour_format ();

	/* We convert this from (0 = Sun, 6 = Sat) to (0 = Mon, 6 = Sun). */
	psi.display_start_weekday = calendar_config_get_week_start_day ();
	psi.display_start_weekday = (psi.display_start_weekday + 6) % 7;

	/* If weekends are compressed then we can't start on a Sunday. */
	if (psi.compress_weekend && psi.display_start_weekday == 6)
		psi.display_start_weekday = 5;

	day_start = time_day_begin_with_zone (whence, zone);
	for (day = 0; day <= psi.days_shown; day++) {
		psi.day_starts[day] = day_start;
		day_start = time_add_day_with_zone (day_start, 1, zone);
	}

	/* Get the events from the server. */
	e_cal_model_generate_instances (model,
					psi.day_starts[0], psi.day_starts[psi.days_shown],
					print_week_summary_cb, &psi);
	qsort (psi.events->data, psi.events->len,
	       sizeof (EWeekViewEvent), e_week_view_event_sort_func);

	/* Layout the events. */
	spans = e_week_view_layout_events (psi.events, NULL,
					   psi.multi_week_view,
					   psi.weeks_shown,
					   psi.compress_weekend,
					   psi.display_start_weekday,
					   psi.day_starts, rows_per_day);

	/* Calculate the size of the cells. */
	if (multi_week_view) {
		cell_width = (right - left) / (psi.compress_weekend ? 6 : 7);
		cell_height = (bottom - top) / (weeks_shown * 2);
	} else {
		cell_width = (right - left) / 2;
		cell_height = (bottom - top) / 6;
	}
	

	/* Calculate the row height, using the normal font and with room for
	   space or a rectangle around it. */
	psi.row_height = font_size * 1.2;
	psi.header_row_height = font_size * 1.5;

	/* Calculate how many rows we can fit into each type of cell. */
	psi.rows_per_cell = ((cell_height * 2) - psi.header_row_height)
		/ psi.row_height;
	psi.rows_per_compressed_cell = (cell_height - psi.header_row_height)
		/ psi.row_height;

	font = get_font_for_size (pr, font_size, GNOME_FONT_BOOK, FALSE);
	/* Draw the grid and the day names/numbers. */
	print_week_view_background (pr, font, &psi, left, top,
				    cell_width, cell_height);
	/* Print the events. */
	for (event_num = 0; event_num < psi.events->len; event_num++) {
		event = &g_array_index (psi.events, EWeekViewEvent, event_num);
		print_week_event (pr, font, &psi, left, top,
				  cell_width, cell_height, model, event, spans);
	}

	pango_font_description_free (font);

	/* Free everything. */
	for (event_num = 0; event_num < psi.events->len; event_num++) {
		event = &g_array_index (psi.events, EWeekViewEvent, event_num);
	}
	g_array_free (psi.events, TRUE);
	g_array_free (spans, TRUE);
}


static void
print_year_summary (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t whence,
		    double left, double right, double top, double bottom,
		    int morerows)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	double row_height, col_width, l, r, t, b;
	time_t now;
	int col, row, rows, cols;

	l = left;
	t = top;

	/* If morerows is set we do 4 rows and 3 columns instead of 3 rows and
	   4 columns. This is useful if we switch paper orientation. */
	if (morerows) {
		rows = 4;
		cols = 3;
	} else {
		rows = 3;
		cols = 4;
	}

	row_height = (top - bottom) / rows;
	col_width = (right - left) / cols;
	r = l + col_width;
	b = top - row_height;
	now = time_year_begin_with_zone (whence, zone);

	for (row = 0; row < rows; row++) {
		t = top - row_height * row;
		b = t - row_height;
		for (col = 0; col < cols; col++) {
			l = left + col_width * col;
			r = l + col_width;
			print_month_small (pr, gcal, now,
					   l + 8, r - 8, t - 8, b + 8,
					   DATE_MONTH, 0, 0, TRUE);
			now = time_add_month_with_zone (now, 1, zone);
		}
	}
}

static void
print_month_summary (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t whence,
		     double left, double right, double top, double bottom)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	time_t date;
	struct tm tm;
	struct icaltimetype tt;
	char buffer[100];
	PangoFontDescription *font;
	gboolean compress_weekend;
	int columns, col, weekday, len, month;
	double font_size, cell_width, x1, x2, y1, y2;

	weekday = calendar_config_get_week_start_day ();
	compress_weekend = calendar_config_get_compress_weekend ();

	/* Remember which month we want. */
	tt = icaltime_from_timet_with_zone (whence, FALSE, zone);
	month = tt.month - 1;

	/* Find the start of the month, and then the start of the week on
	   or before that day. */
	date = time_month_begin_with_zone (whence, zone);
	date = time_week_begin_with_zone (date, weekday, zone);

	/* If weekends are compressed then we can't start on a Sunday. */
	if (compress_weekend && weekday == 0)
		date = time_add_day_with_zone (date, -1, zone);

	/* do day names ... */

	/* We are only interested in outputting the weekday here, but we want
	   to be able to step through the week without worrying about
	   overflows making strftime choke, so we move near to the start of
	   the month. */
	tm = *convert_timet_to_struct_tm (date, zone);
	tm.tm_mday = (tm.tm_mday % 7) + 7;

	font = get_font_for_size (pr, MONTH_NORMAL_FONT_SIZE, GNOME_FONT_BOLD, FALSE);
	font_size = get_font_size (font);

	columns = compress_weekend ? 6 : 7;
	cell_width = (right - left) / columns;
	y1 = top;
	y2 = top + font_size * 1.5;

	for (col = 0; col < columns; col++) {
		if (tm.tm_wday == 6 && compress_weekend) {
			e_utf8_strftime (buffer, sizeof (buffer), "%a/", &tm);
			len = strlen (buffer);
			tm.tm_mday++;
			tm.tm_wday = (tm.tm_wday + 1) % 7;
			e_utf8_strftime (buffer + len, sizeof (buffer) - len,
				  "%a", &tm);
		} else {
			e_utf8_strftime (buffer, sizeof (buffer), "%A", &tm);
		}

		x1 = left + cell_width * col;
		x2 = x1 + cell_width;

		print_border (pr->pc, x1, x2, y1, y2, 1.0, -1.0);
		print_text_size (pr, buffer, ALIGN_CENTER, x1, x2, y1, y2);

		tm.tm_mday++;
		tm.tm_wday = (tm.tm_wday + 1) % 7;
	}
	pango_font_description_free (font);

	top = y2;
	print_week_summary (pr, gcal, date, TRUE, 6, month,
			    MONTH_NORMAL_FONT_SIZE,
			    left, right, top, bottom);
}


static void
print_todo_details (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal,
		    time_t start, time_t end,
		    double left, double right, double top, double bottom)
{
	PangoFontDescription *font_summary;
	double y, yend, x, xend;
	struct icaltimetype *tt;
	ECalendarTable *task_pad;
	ETable *table;
	ECalModel *model;
	gint rows, row;
	cairo_t *cr;

	/* We get the tasks directly from the TaskPad ETable. This means we
	   get them filtered & sorted for free. */
	task_pad = gnome_calendar_get_task_pad (gcal);
	table = e_calendar_table_get_table (task_pad);
	model = e_calendar_table_get_model (task_pad);

	font_summary = get_font_for_size (pr, 12, GNOME_FONT_BOOK, FALSE);

	cr = gtk_print_context_get_cairo_context (pr->pc);

	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_set_line_width (cr, 0.0);
	top +=2;

	titled_box (pr, _("Tasks"), font_summary, ALIGN_CENTER | ALIGN_BORDER,
		    &left, &right, &top, &bottom, 1.0);

	y = top;
	yend = bottom - 2;

	rows = e_table_model_row_count (E_TABLE_MODEL (model));
	for (row = 0; row < rows; row++) {
		ECalModelComponent *comp_data;
		ECalComponent *comp;
		ECalComponentText summary;
		int model_row;

		model_row = e_table_view_to_model_row (table, row);
		comp_data = e_cal_model_get_component_at (model, model_row);
		if (!comp_data)
			continue;

		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (comp_data->icalcomp));

		e_cal_component_get_summary (comp, &summary);
		if (!summary.value) {
			g_object_unref (comp);
			continue;
		}

		x = left;
		xend = right - 2;
		if (y > bottom)
			break;

		/* Print the box to put the tick in. */
		print_border (pr->pc, x + 2, x + 8, y + 6, y + 15, 0.1, -1.0);

		/* If the task is complete, print a tick in the box. */
		e_cal_component_get_completed (comp, &tt);
		if (tt) {
			e_cal_component_free_icaltimetype (tt);

			cr = gtk_print_context_get_cairo_context (pr->pc);
			cairo_set_source_rgb (cr, 0, 0, 0);
			cairo_move_to (cr, x + 3, y + 11);
			cairo_line_to (cr, x + 5, y + 14);
			cairo_line_to (cr, x + 7, y + 5.5);
			cairo_set_line_width (cr, 1);
			cairo_stroke (cr);
		}

		y = bound_text (pr, font_summary, summary.value,
				x + 14, xend, y+4, yend, 0);


		y += get_font_size (font_summary)-5;
		cr = gtk_print_context_get_cairo_context (pr->pc);
		cairo_move_to (cr, x, y);
		cairo_line_to (cr, xend, y);
		cairo_set_line_width (cr, 1);
		cairo_stroke (cr);

		g_object_unref (comp);
	}

	pango_font_description_free (font_summary);
}


/* Value for the PrintView enum */
static const int print_view_map[] = {
	PRINT_VIEW_DAY,
	PRINT_VIEW_WEEK,
	PRINT_VIEW_MONTH,
	PRINT_VIEW_YEAR,
	-1
};

/* Creates the range selector widget for printing a calendar */
static GtkWidget *
range_selector_new (GtkWidget *dialog, time_t at, int *view)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	GtkWidget *box;
	GtkWidget *radio;
	GSList *group;
	char text[1024];
	char str1[512];
	char str2[512];
	struct tm tm;
	time_t week_begin, week_end;
	struct tm week_begin_tm, week_end_tm;
	gint week_start_day;

	box = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);

	tm = *convert_timet_to_struct_tm (at, zone);

	/* Day */

	e_utf8_strftime (text, sizeof (text), _("Selected day (%a %b %d %Y)"), &tm);
	radio = gtk_radio_button_new_with_label (NULL, text);
	group = gtk_radio_button_group (GTK_RADIO_BUTTON (radio));
	gtk_box_pack_start (GTK_BOX (box), radio, FALSE, FALSE, 0);

	/* Week */

	week_start_day = calendar_config_get_week_start_day ();
	week_begin = time_week_begin_with_zone (at, week_start_day, zone);
	/* If the week starts on a Sunday, we have to show the Saturday first,
	   since the weekend is compressed. If the original date passed in was
	   a Saturday, we need to move on to the next Saturday, else we move
	   back to the last one. */
	if (week_start_day == 0) {
		if (tm.tm_wday == 6)
			week_begin = time_add_day_with_zone (week_begin, 6, zone);
		else
			week_begin = time_add_day_with_zone (week_begin, -1, zone);
	}
	week_end = time_add_day_with_zone (week_begin, 6, zone);

	week_begin_tm = *convert_timet_to_struct_tm (week_begin, zone);
	week_end_tm = *convert_timet_to_struct_tm (week_end, zone);

	if (week_begin_tm.tm_mon == week_end_tm.tm_mon) {
		e_utf8_strftime (str1, sizeof (str1), _("%a %b %d"), &week_begin_tm);
		e_utf8_strftime (str2, sizeof (str2), _("%a %d %Y"), &week_end_tm);
	} else {
		if (week_begin_tm.tm_year == week_end_tm.tm_year) {
			e_utf8_strftime (str1, sizeof (str1), _("%a %b %d"), &week_begin_tm);
			e_utf8_strftime (str2, sizeof (str2), _("%a %b %d %Y"), &week_end_tm);
		} else {
			e_utf8_strftime (str1, sizeof (str1), _("%a %b %d %Y"), &week_begin_tm);
			e_utf8_strftime (str2, sizeof (str2), _("%a %b %d %Y"), &week_end_tm);
		}
	}

	g_snprintf (text, sizeof (text), _("Selected week (%s - %s)"), str1, str2);

	radio = gtk_radio_button_new_with_label (group, text);
	group = gtk_radio_button_group (GTK_RADIO_BUTTON (radio));
	gtk_box_pack_start (GTK_BOX (box), radio, FALSE, FALSE, 0);

	/* Month */

	e_utf8_strftime (text, sizeof (text), _("Selected month (%b %Y)"), &tm);
	radio = gtk_radio_button_new_with_label (group, text);
	group = gtk_radio_button_group (GTK_RADIO_BUTTON (radio));
	gtk_box_pack_start (GTK_BOX (box), radio, FALSE, FALSE, 0);

	/* Year */

	e_utf8_strftime (text, sizeof (text), _("Selected year (%Y)"), &tm);
	radio = gtk_radio_button_new_with_label (group, text);
	group = gtk_radio_button_group (GTK_RADIO_BUTTON (radio));
	gtk_box_pack_start (GTK_BOX (box), radio, FALSE, FALSE, 0);

	/* Select default */

	e_dialog_widget_hook_value (dialog, radio, view, (gpointer) print_view_map);

	gtk_widget_show_all (box);
	return box;
}


static void
print_day_view (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t date,
		double left, double right, double top, double bottom)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	int i, days = 1;
	double todo, header, l;
	char buf[100];
	cairo_t *cr;

	for (i = 0; i < days; i++) {
		todo = (right - left) * 0.75 + left;
 	/*		header = top - HEADER_HEIGHT;*/
		header = HEADER_HEIGHT + top;

		cr = gtk_print_context_get_cairo_context (pr->pc);

		/* Print the main view with all the events in. */
		print_day_details (pr, gcal, date,
				   left, todo - 2.0, header, bottom);
		
		 /* Print the TaskPad down the right. */
		print_todo_details (pr, gcal, 0, INT_MAX,
				    todo, right, header, bottom);
	
		/* Print the filled border around the header. */
		print_border (pr->pc, left, right, top, header + 2.0, 1.0, 0.9);
		
		/* Print the 2 mini calendar-months. */
		l = right - SMALL_MONTH_PAD - SMALL_MONTH_WIDTH * 2  - SMALL_MONTH_SPACING;

		 print_month_small (pr, gcal, date,
				   l, l + SMALL_MONTH_WIDTH,
				   top + 4, header + 4,
				   DATE_MONTH | DATE_YEAR, date, date, FALSE);
		

		l += SMALL_MONTH_SPACING + SMALL_MONTH_WIDTH;
		print_month_small (pr, gcal,
				   time_add_month_with_zone (date, 1, zone),
				   l, l + SMALL_MONTH_WIDTH,
				   top + 4, header + 4,
				   DATE_MONTH | DATE_YEAR, 0, 0, FALSE);
	  
	 
		/* Print the date, e.g. '8th May, 2001'. */
		format_date (date, DATE_DAY | DATE_MONTH | DATE_YEAR,
			     buf, 100);

		print_text_size_bold (pr, buf, ALIGN_LEFT,
				      left + 4, todo, top + 4, top + 4 + 24);

		/* Print the day, e.g. 'Tuesday'. */
		format_date (date, DATE_DAYNAME, buf, 100);

		print_text_size_bold (pr, buf, ALIGN_LEFT,
				 left + 4, todo, top + 32, top + 32 + 18);

		date = time_add_day_with_zone (date, 1, zone);
	 }
}


static void
print_week_view (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t date,
		 double left, double right, double top, double bottom)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	double header, l;
	char buf[100];
	time_t when;
	gint week_start_day;
	struct tm tm;

	header = HEADER_HEIGHT - top;
	tm = *convert_timet_to_struct_tm (date, zone);
	week_start_day = calendar_config_get_week_start_day ();
	when = time_week_begin_with_zone (date, week_start_day, zone);

	/* If the week starts on a Sunday, we have to show the Saturday first,
	   since the weekend is compressed. */
	if (week_start_day == 0) {
		if (tm.tm_wday == 6)
			when = time_add_day_with_zone (when, 6, zone);
		else
			when = time_add_day_with_zone (when, -1, zone);
	}

	/* Print the main week view. */
	print_week_summary (pr, gcal, when, FALSE, 1, 0,
			    WEEK_NORMAL_FONT_SIZE,
			    left, right, header + 20, bottom);
	
	
	/* Print the border around the main view. */
	print_border (pr->pc, left, right, header , bottom, 1.0, -1.0);

	/* Print the border around the header area. */
	print_border (pr->pc, left, right, top, header + 2.0 + 20, 1.0, 0.9);

	/* Print the 2 mini calendar-months. */
	l = right - SMALL_MONTH_PAD - SMALL_MONTH_WIDTH * 2
		- SMALL_MONTH_SPACING;
	print_month_small (pr, gcal, when,
			   l, l + SMALL_MONTH_WIDTH,
			   top + 4, header + 30,
			   DATE_MONTH | DATE_YEAR, when,
			   time_add_week_with_zone (when, 1, zone), FALSE);

	l += SMALL_MONTH_SPACING + SMALL_MONTH_WIDTH;
	print_month_small (pr, gcal,
			   time_add_month_with_zone (when, 1, zone),
			   l, l + SMALL_MONTH_WIDTH,
			   top + 4, header + 30,
			   DATE_MONTH | DATE_YEAR, when,
			   time_add_week_with_zone (when, 1, zone), FALSE);

	/* Print the start day of the week, e.g. '7th May 2001'. */
	format_date (when, DATE_DAY | DATE_MONTH | DATE_YEAR, buf, 100);
	print_text_size_bold (pr, buf, ALIGN_LEFT,
			      left + 3, right, top + 4, top + 4 + 24);

	/* Print the end day of the week, e.g. '13th May 2001'. */
	when = time_add_day_with_zone (when, 6, zone);
	format_date (when, DATE_DAY | DATE_MONTH | DATE_YEAR, buf, 100);
	print_text_size_bold (pr, buf, ALIGN_LEFT,
			      left + 3, right, top + 24 + 3, top + 24 + 3 + 24);
}


static void
print_month_view (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t date,
		  double left, double right, double top, double bottom)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	double header;
	char buf[100];

	header = top + HEADER_HEIGHT;

	/* Print the main month view. */
	print_month_summary (pr, gcal, date, left, right, header, bottom);

	/* Print the border around the header. */
	print_border (pr->pc, left, right, top, header, 1.0, 0.9);

	/* Print the 2 mini calendar-months. */
	print_month_small (pr, gcal,
			   time_add_month_with_zone (date, 1, zone),
			   right - (right - left) / 7 + 2, right - 8,
			   top + 4, header,
			   DATE_MONTH | DATE_YEAR, 0, 0, FALSE);
	print_month_small (pr, gcal,
			   time_add_month_with_zone (date, -1, zone),
			   left + 8, left + (right - left) / 7 - 2,
			   top + 4, header,
			   DATE_MONTH | DATE_YEAR, 0, 0, FALSE);

	/* Print the month, e.g. 'May 2001'. */
	format_date (date, DATE_MONTH | DATE_YEAR, buf, 100);
	print_text_size_bold (pr, buf, ALIGN_CENTER,
			      left + 3, right - 3, top + 3, top + 3 + 24);

}


static void
print_year_view (EvoCalendarPrintRenderer *pr, GnomeCalendar *gcal, time_t date,
		 double left, double right, double top, double bottom)
{
	char buf[100];
	cairo_t *cr;

	cr = gtk_print_context_get_cairo_context (pr->pc); 

	cairo_show_page (cr);
	print_year_summary (pr, gcal, date, left, right, top + 50, bottom,
			    TRUE);

	/* centered title */
	format_date (date, DATE_YEAR, buf, 100);
	print_text_size_bold (pr, buf, ALIGN_CENTER,
			      left+3, right, top+3, top + 27);
	cr=gtk_print_context_get_cairo_context (pr->pc);
	cairo_show_page (cr);
}

static void
write_label_piece (time_t t, char *buffer, int size, char *stext, char *etext)
{
	icaltimezone *zone = calendar_config_get_icaltimezone ();
	struct tm *tmp_tm;
	int len;

	tmp_tm = convert_timet_to_struct_tm (t, zone);

	if (stext != NULL)
		strcat (buffer, stext);

	len = strlen (buffer);
	e_time_format_date_and_time (tmp_tm,
				     calendar_config_get_24_hour_format (),
				     FALSE, FALSE,
				     &buffer[len], size - len);
	if (etext != NULL)
		strcat (buffer, etext);
}

static icaltimezone*
get_zone_from_tzid (ECal *client, const char *tzid)
{
	icaltimezone *zone;

	/* Note that the timezones may not be on the server, so we try to get
	   the builtin timezone with the TZID first. */
	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!zone) {
		if (!e_cal_get_timezone (client, tzid, &zone, NULL)) 
			/* FIXME: Handle error better. */
			g_warning ("Couldn't get timezone from server: %s",
				   tzid ? tzid : "");
	}

	return zone;
}

static void
print_date_label (EvoCalendarPrintRenderer *pr, ECalComponent *comp, ECal *client,
		  double left, double right, double top, double bottom)
{
	icaltimezone *start_zone, *end_zone, *due_zone, *completed_zone;
	ECalComponentDateTime datetime;
	time_t start = 0, end = 0, complete = 0, due = 0;
	static char buffer[1024];

	e_cal_component_get_dtstart (comp, &datetime);
	if (datetime.value) {
		start_zone = get_zone_from_tzid (client, datetime.tzid);
		if (!start_zone || datetime.value->is_date)
			start_zone = calendar_config_get_icaltimezone ();
		start = icaltime_as_timet_with_zone (*datetime.value,
						     start_zone);
	}
	e_cal_component_free_datetime (&datetime);

	e_cal_component_get_dtend (comp, &datetime);
	if (datetime.value) {
		end_zone = get_zone_from_tzid (client, datetime.tzid);
		if (!end_zone || datetime.value->is_date)
			end_zone = calendar_config_get_icaltimezone ();
		end = icaltime_as_timet_with_zone (*datetime.value,
						   end_zone);
	}
	e_cal_component_free_datetime (&datetime);

	e_cal_component_get_due (comp, &datetime);
	if (datetime.value) {
		due_zone = get_zone_from_tzid (client, datetime.tzid);
		if (!due_zone || datetime.value->is_date)
			due_zone = calendar_config_get_icaltimezone ();
		due = icaltime_as_timet_with_zone (*datetime.value,
						   due_zone);
	}
	e_cal_component_free_datetime (&datetime);

	e_cal_component_get_completed (comp, &datetime.value);
	if (datetime.value) {
		completed_zone = icaltimezone_get_utc_timezone ();
		complete = icaltime_as_timet_with_zone (*datetime.value,
							completed_zone);
		e_cal_component_free_icaltimetype (datetime.value);
	}

	buffer[0] = '\0';

	if (start > 0)
		write_label_piece (start, buffer, 1024, NULL, NULL);

	if (end > 0 && start > 0)
		write_label_piece (end, buffer, 1024, _(" to "), NULL);

	if (complete > 0) {
		if (start > 0)
			write_label_piece (complete, buffer, 1024, _(" (Completed "), ")");
		else
			write_label_piece (complete, buffer, 1024, _("Completed "), NULL);
	}

	if (due > 0 && complete == 0) {
		if (start > 0)
			write_label_piece (due, buffer, 1024, _(" (Due "), ")");
		else
			write_label_piece (due, buffer, 1024, _("Due "), NULL);
	}

	print_text_size_bold (pr, buffer, ALIGN_LEFT,
			      left, right, top, top - 15);
}

static void
print_comp_item (PrintCompItem *pci)
		 
{
	PangoFontDescription *font;
	EvoCalendarPrintRenderer *pr;
	ECal *client;
	ECalComponent *comp;
	ECalComponentVType vtype;
	ECalComponentText text;
	GSList *desc, *l;
	GSList *contact_list, *elem;

	const char *title, *categories, *location;
	char *categories_string, *location_string, *summary_string;
	double header_size;
	cairo_t *cr;
	double left, right, top, bottom;

        /* PrintCompItem structure contains elements to be used 
         * with the Print Context , obtained in comp_draw_page
         */    
	comp = g_new0 (ECalComponent, 1);
	client = g_new0 (ECal, 1);
	pr = pci->pr;
	client = pci->client;
	comp = pci->comp;
	left = pci->l;
	right = pci->r;
	top = pci->t;
	bottom = pci->b;

	vtype = e_cal_component_get_vtype (comp);

	/* We should only be asked to print VEVENTs, VTODOs, or VJOURNALs. */
	if (vtype == E_CAL_COMPONENT_EVENT)
		title = _("Appointment");
	else if (vtype == E_CAL_COMPONENT_TODO)
		title = _("Task");
	else if (vtype == E_CAL_COMPONENT_JOURNAL)
		title = _("Memo");
	else
		return;

	cr = gtk_print_context_get_cairo_context (pr->pc);
	    
	/* Print the title in a box at the top of the page. */
	font = get_font_for_size (pr, 18, GNOME_FONT_BOLD, FALSE);
	header_size = 40;
	print_border (pr->pc, left, right, top, top+header_size,
		      1.0, 0.9);
	print_text (pr, font, title, ALIGN_CENTER, left, right,
		    top + 0.1, top +header_size - 0.1);
	pango_font_description_free (font);

	top += header_size + 30;
	
	/* Summary */
	font = get_font_for_size (pr, 18, GNOME_FONT_BOLD, FALSE);
	e_cal_component_get_summary (comp, &text);
	summary_string = g_strdup_printf (_("Summary: %s"), text.value);
	top = bound_text (pr, font, summary_string, left, right,
			  top, bottom, 0);
	
	g_free (summary_string);

	/* Location */
	e_cal_component_get_location (comp, &location);
	if (location && location[0]) {
		location_string = g_strdup_printf (_("Location: %s"),
						   location);
		top = bound_text (pr, font, location_string, left, right,
				  top + 3, bottom, 0);
		g_free (location_string);
	}
	pango_font_description_free (font);
	
	/* Date information */
	print_date_label (pr, comp, client, left, right, top + 3, top + 15);
	top += 20;
	font = get_font_for_size (pr, 12, GNOME_FONT_BOOK, FALSE);
	
	/* For a VTODO we print the Status, Priority, % Complete and URL. */
	if (vtype == E_CAL_COMPONENT_TODO) {
		icalproperty_status status;
		const char *status_string = NULL;
		int *percent;
		int *priority;
		const char *url;
		
		/* Status */
		e_cal_component_get_status (comp, &status);
		if (status != ICAL_STATUS_NONE) {
			switch (status) {
			case ICAL_STATUS_NEEDSACTION:
				status_string = _("Not Started");
				break;
			case ICAL_STATUS_INPROCESS:
				status_string = _("In Progress");
				break;
			case ICAL_STATUS_COMPLETED:
				status_string = _("Completed");
				break;
			case ICAL_STATUS_CANCELLED:
				status_string = _("Canceled");
				break;
			default:
				break;
			}

			if (status_string) {
				char *status_text = g_strdup_printf (_("Status: %s"),
							      status_string);
				top = bound_text (pr, font, status_text,
						  left, right, top, bottom, 0);
				top += get_font_size (font) - 6;
				g_free (status_text);
			}
		}

		/* Priority */
		e_cal_component_get_priority (comp, &priority);
		if (priority && *priority >= 0) {
			char *priority_string, *pri_text;

			priority_string = e_cal_util_priority_to_string (*priority);
			e_cal_component_free_priority (priority);

			pri_text = g_strdup_printf (_("Priority: %s"), priority_string);
			top = bound_text (pr, font, pri_text,
					  left, right, top, bottom, 0);
			top += get_font_size (font) - 6;
			g_free (pri_text);
		}

		/* Percent Complete */
		e_cal_component_get_percent (comp, &percent);
		if (percent) {
			char *percent_string;

			percent_string = g_strdup_printf (_("Percent Complete: %i"), *percent);
			e_cal_component_free_percent (percent);

			top = bound_text (pr, font, percent_string,
					  left, right, top, bottom, 0);
			top += get_font_size (font) - 6;
		}


		/* URL */
		e_cal_component_get_url (comp, &url);
		if (url && url[0]) {
			char *url_string = g_strdup_printf (_("URL: %s"),
							    url);

			top = bound_text (pr, font, url_string,
					  left, right, top, bottom, 0);
			top += get_font_size (font) - 6;
			g_free (url_string);
		}
	}
	
	/* Categories */
	e_cal_component_get_categories (comp, &categories);
	if (categories && categories[0]) {
		categories_string = g_strdup_printf (_("Categories: %s"),
						     categories);
		top = bound_text (pr, font, categories_string,
				  left, right, top, bottom, 0);
		top += get_font_size (font) - 6 ;
		g_free (categories_string);
	}
	
	/* Contacts */
	e_cal_component_get_contact_list (comp, &contact_list);
	if (contact_list) {
		GString *contacts = g_string_new (_("Contacts: "));
		for (elem = contact_list; elem; elem = elem->next) {
			ECalComponentText *t = elem->data;
			/* Put a comma between contacts. */
			if (elem != contact_list)
				g_string_append (contacts, ", ");
			g_string_append (contacts, t->value);
		}
		e_cal_component_free_text_list (contact_list);

		top = bound_text (pr, font, contacts->str,
				  left, right, top, bottom, 0);
		top += get_font_size (font) - 6;
		g_string_free (contacts, TRUE);
	}
	top += 16;

	/* Description */
	e_cal_component_get_description_list (comp, &desc);
	for (l = desc; l != NULL; l = l->next) {
		ECalComponentText *text = l->data;
		if (top > bottom) {
		top = 10; //FIXME
		cairo_show_page (cr);
		}
		if (text->value != NULL) 
			top = bound_text (pr, font, text->value, left, right, top + 3, bottom, 0) + 30;

	}

	e_cal_component_free_text_list (desc);
	pango_font_description_free (font);

}

void
print_calendar (GnomeCalendar *gcal, gboolean preview, time_t date,
		PrintView default_view)
{
	GtkPrintSettings *settings;
	GtkPrintOperationResult res;
	GtkPrintOperation *print;
	GtkPaperSize *paper_size;
	GtkPageSetup *page_setup;
	GtkWidget *gpd;
	GtkWidget *range;
	PrintCalItem *pcali;
	int view;
	double  r, t, w, h;

	g_return_if_fail (gcal != NULL);
	g_return_if_fail (GNOME_IS_CALENDAR (gcal));
	
	view = (int) default_view;

	pcali = g_new0 (PrintCalItem, 1);
	pcali->gcal = g_new0 (GnomeCalendar, 1);

	print = gtk_print_operation_new ();
	paper_size = gtk_paper_size_new ("iso_a4");
	page_setup = gtk_page_setup_new();
	gtk_page_setup_set_paper_size(page_setup, paper_size);
	range = range_selector_new (gpd, date, &view);

	settings = gtk_print_settings_new ();
	settings = e_print_load_settings ();
	gpd = e_print_get_dialog_with_config (_("Print Item"), 0, settings);

	t = gtk_paper_size_get_default_top_margin (paper_size, GTK_UNIT_POINTS);
	r = gtk_paper_size_get_default_right_margin (paper_size, GTK_UNIT_POINTS);
	w = gtk_paper_size_get_width (paper_size, GTK_UNIT_POINTS);
	h = gtk_paper_size_get_height (paper_size, GTK_UNIT_POINTS);
	
	pcali->b = h * (1.0 - TEMP_MARGIN);  
        pcali->l = r * TEMP_MARGIN;
        pcali->t = t * (1.0 - TEMP_MARGIN); 
        pcali->r = w * (1.0 - TEMP_MARGIN);
	pcali->default_view = default_view;
	pcali->gcal = gcal;
	pcali->date = date;

	gtk_print_operation_set_default_page_setup (print, page_setup);
	gtk_print_operation_set_n_pages (print, 1);
        	
	/* FIX ME ,Allow it to use the default settings for the first time */
        gtk_print_operation_set_print_settings (print, settings); 

	g_signal_connect (print, "create-custom-widget", G_CALLBACK (create_custom_widget), range);
	g_signal_connect (print, "custom-widget-apply", G_CALLBACK (apply_custom_widget), NULL);
        g_signal_connect (print, "draw_page", G_CALLBACK (cal_draw_page), pcali);

		/* Runs the print dialog, recursively, emitting signals */
	if (preview)
		gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PREVIEW, NULL, NULL);
	else
		res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, NULL, NULL);   
    
	/* Save the user configured settings back to the gconf */
	settings = gtk_print_operation_get_print_settings (print);
	e_print_save_settings (settings); 
	
	if (res == GTK_PRINT_OPERATION_RESULT_APPLY) {
		   g_object_unref (print); 
	}
	else if (res == GTK_PRINT_OPERATION_RESULT_ERROR) {
		/* FIX ME */
		     
	}

}

void
print_comp (ECalComponent *comp, ECal *client, gboolean preview)
{
        GtkPrintOperation *print;
	GtkPrintSettings *settings;
	GtkPaperSize *paper_size;
        PrintCompItem *pci;
	GtkPrintOperationResult res;
	GtkPageSetup *page_setup;
        gdouble t, r, w,h;
        GtkWidget *gpd;

        g_return_if_fail (comp != NULL);
        g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	pci = g_new0 (PrintCompItem, 1);

	print = gtk_print_operation_new ();
	paper_size = gtk_paper_size_new ("iso_a4");
	page_setup = gtk_page_setup_new();
	gtk_page_setup_set_paper_size(page_setup, paper_size);

	settings = gtk_print_settings_new ();
	settings = e_print_load_settings ();
	gpd = e_print_get_dialog_with_config (_("Print Item"), 0, settings);
	
	/* Values using the print context , used in comp_draw_page */
	t = gtk_paper_size_get_default_top_margin (paper_size, GTK_UNIT_POINTS);
	r = gtk_paper_size_get_default_right_margin (paper_size, GTK_UNIT_POINTS);
	w = gtk_paper_size_get_width (paper_size, GTK_UNIT_POINTS);
	h = gtk_paper_size_get_height (paper_size, GTK_UNIT_POINTS);
	pci->b = h * (1.0 - TEMP_MARGIN);
        pci->l = r * TEMP_MARGIN;
        pci->t = t * (1.0 - TEMP_MARGIN);
        pci->r = w * (1.0 - TEMP_MARGIN);
        pci->comp = comp;
        pci->client = client;

	gtk_print_operation_set_default_page_setup (print, page_setup);
	gtk_print_operation_set_n_pages (print, 1);
	
        /* FIX ME ,Allow it to use the default settings for the first time */
        gtk_print_operation_set_print_settings (print, settings); 

        g_signal_connect (print, "draw_page", G_CALLBACK (comp_draw_page), pci);
	
	gtk_print_operation_set_print_settings (print, settings);
	
	/* Runs a dialog , emitting various signals based on user response */
	if (preview)
		gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, NULL, NULL);
	else
		res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG , NULL, NULL); 

	/* Saves the user configuration in gconf */
	settings = gtk_print_operation_get_print_settings (print);
	e_print_save_settings (settings);

	 if (res == GTK_PRINT_OPERATION_RESULT_APPLY) {
		  g_object_unref (print);
	      }
	 else if ( res == GTK_PRINT_OPERATION_RESULT_ERROR) {
		 /* FIX ME */
	 }	  
}

static void
print_title (GtkPrintContext *context, const char *title,
	     double page_width, double page_height, double left_margin, double bottom_margin)
{
	PangoFontDescription *font;
	PangoLayout *pl;
	double w, x, y;
	cairo_t *cr;

	cr = gtk_print_context_get_cairo_context (context);
	cairo_save (cr);

	pl = gtk_print_context_create_pango_layout (context);
	font = pango_font_description_from_string("Sans Bold 18");
	pango_layout_set_font_description (pl, font);
	pango_layout_set_text (pl, title, -1);
	
	w = pango_layout_get_width (pl)/(double)PANGO_SCALE;
	y = page_height ;
	x = (page_width - w) / 2;

	cairo_move_to (cr, x, y );
	pango_cairo_show_layout (cr, pl);

	cairo_restore (cr);
	pango_font_description_free (font); 

}

void
print_table (ETable *etable, const char *dialog_title, const char *print_header, gboolean preview)
{
	EPrintable *printable;
	GtkPrintOperationResult res;
	GtkPrintSettings *settings;
	GtkPrintOperation *print;
	GtkPageSetup *page_setup;
	GtkPaperSize *paper_size;
	GtkWidget *gpd;
	PrintTableItem *pti;
	double w, h, t, r, l, b;

	pti = g_new0 (PrintTableItem, 1);

	print = gtk_print_operation_new ();
 	paper_size = gtk_paper_size_new ("iso_a4");
	page_setup = gtk_page_setup_new();
	gtk_page_setup_set_paper_size(page_setup, paper_size);

	w = gtk_paper_size_get_width (paper_size, GTK_UNIT_POINTS);
	h = gtk_paper_size_get_height (paper_size, GTK_UNIT_POINTS);
	t = gtk_paper_size_get_default_top_margin (paper_size, GTK_UNIT_POINTS);
	r = gtk_paper_size_get_default_right_margin (paper_size, GTK_UNIT_POINTS);

	b =h * (1.0 - TEMP_MARGIN);  
        l =r * TEMP_MARGIN;
        t =t * (1.0 - TEMP_MARGIN); 
        r =w * (1.0 - TEMP_MARGIN);
	
	pti->left_margin = l;
	pti->bottom_margin = b;
	pti->paper_width = r - l;
	pti->paper_height= t + 10;

	settings = e_print_load_settings ();
	gpd = e_print_get_dialog_with_config (dialog_title, 0, settings);
	printable = e_table_get_printable (etable);

	g_object_ref (printable);
	gtk_object_sink (GTK_OBJECT (printable));
	e_printable_reset (printable);
	pti->printable = printable;

	gtk_print_operation_set_default_page_setup (print, page_setup);
	gtk_print_operation_set_n_pages (print, 1);

	/* FIX ME ,Allow it to use the default settings for the first time */
        gtk_print_operation_set_print_settings (print, settings); 

	/* runs the dialog emitting the signals based on user response */
	g_signal_connect (print, "draw_page", G_CALLBACK (table_draw_page), pti);
	if (!preview)
		res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, NULL, NULL);
	else
		gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PREVIEW, NULL, NULL);     
	

	/* Saves the user configuration in gconf */
	settings = gtk_print_operation_get_print_settings (print);
	e_print_save_settings (settings);

	 if (res == GTK_PRINT_OPERATION_RESULT_APPLY) {
		  g_object_unref (print);
	      }
	 else if ( res == GTK_PRINT_OPERATION_RESULT_ERROR) {
		 /* FIX ME */
	 }	  
}

static void
comp_draw_page (GtkPrintOperation *print, GtkPrintContext *context, gint page_nr, PrintCompItem *pci)
{
	pci = evo_calendar_print_data_new (context, pci);
	print_comp_item (pci);
}

static void
cal_draw_page(GtkPrintOperation *print1, GtkPrintContext *context, gint page_nr1, PrintCalItem *pcali)
{
	pcali = evo_calendar_print_cal_data_new (context, pcali);
		
	switch (pcali->default_view) {
	case PRINT_VIEW_DAY:
		print_day_view (pcali->pr, pcali->gcal, pcali->date, pcali->l, pcali->r, pcali->t, pcali->b);
		break;
	case PRINT_VIEW_WEEK:
		print_week_view (pcali->pr, pcali->gcal, pcali->date, pcali->l, pcali->r, pcali->t, pcali->b);
		break;
	case PRINT_VIEW_MONTH:
		print_month_view (pcali->pr, pcali->gcal, pcali->date, pcali->l, pcali->r, pcali->t, pcali->b);
		break;
	case PRINT_VIEW_YEAR:
		print_year_view (pcali->pr, pcali->gcal, pcali->date, pcali->l, pcali->r, pcali->t, pcali->b);
		break;
	default:
		g_assert_not_reached ();
	}  
}

static void
table_draw_page (GtkPrintOperation *print, GtkPrintContext *context, gint page_nr, PrintTableItem *pti)
{
		pti->context = context;
	
	do {
		print_title (context,"Table", pti->paper_width, pti->paper_height, pti->left_margin, pti->bottom_margin);
		
		if (e_printable_data_left (pti->printable))
			e_printable_print_page (pti->printable, pti->context,
						pti->paper_width, pti->paper_height + 24, TRUE);

          } while (e_printable_data_left (pti->printable));
}

/*
 * This callback creates a custom widget that gets inserted into the
 * print operation dialog, the custom widget contains the range,
 * day, view, month, or year
 */

GObject* create_custom_widget (GtkPrintOperation *print,GtkWidget *range)
{
	return range;
}

/*
 * Read the information from the custom widget and print day, view, or 
 * month based on user selection 
 */

static void
apply_custom_widget (GtkPrintOperation *print, GtkWidget *dialog, PrintCalItem *cpi)
{
	gchar str;
	str = e_dialog_editable_get (dialog);	
	printf("%sValue of Radio Button \n", str);
}

