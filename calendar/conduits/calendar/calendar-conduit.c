/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Calendar Conduit
 *
 * Copyright (C) 1998 Free Software Foundation
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Authors: Eskil Heyn Olsen <deity@eskil.dk> 
 *          JP Rosevear <jpr@helixcode.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <errno.h>

#include <liboaf/liboaf.h>
#include <bonobo.h>
#include <gnome-xml/parser.h>
#include <cal-client/cal-client.h>
#include <cal-util/timeutil.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-file.h>
#include <pi-dlp.h>
#include <pi-version.h>
#include <libical/src/libical/icaltypes.h>
#include <e-pilot-map.h>

#define CAL_CONFIG_LOAD 1
#define CAL_CONFIG_DESTROY 1
#include <calendar-conduit-config.h>
#undef CAL_CONFIG_LOAD
#undef CAL_CONFIG_DESTROY

#include <calendar-conduit.h>

GnomePilotConduit * conduit_get_gpilot_conduit (guint32);
void conduit_destroy_gpilot_conduit (GnomePilotConduit*);

#define CONDUIT_VERSION "0.1.0"
#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "ecalconduit"

#define DEBUG_CALCONDUIT 1
/* #undef DEBUG_CALCONDUIT */

#ifdef DEBUG_CALCONDUIT
#define LOG(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)
#else
#define LOG(e...)
#endif 

#define WARN(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, e)
#define INFO(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)

/* debug spew DELETE ME */
static char *
print_local (ECalLocalRecord *local)
{
	static char buff[ 4096 ];

	if (local == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	if (local->appt && local->appt->description) {
		sprintf (buff, "[%ld %ld '%s' '%s']",
			 mktime (&local->appt->begin),
			 mktime (&local->appt->end),
			 local->appt->description,
			 local->appt->note);
		return buff;
	}

	return "";
	
	return cal_component_get_as_string (local->comp);
}


/* debug spew DELETE ME */
static char *print_remote (GnomePilotRecord *remote)
{
	static char buff[ 4096 ];
	struct Appointment appt;

	if (remote == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	memset (&appt, 0, sizeof (struct Appointment));
	unpack_Appointment (&appt, remote->record, remote->length);

	sprintf (buff, "[%ld %ld '%s' '%s']",
		 mktime (&appt.begin),
		 mktime (&appt.end),
		 appt.description,
		 appt.note);

	return buff;
}

/* Context Routines */
static void
e_calendar_context_new (ECalConduitContext **ctxt, guint32 pilot_id) 
{
	*ctxt = g_new0 (ECalConduitContext,1);
	g_assert (ctxt!=NULL);

	calconduit_load_configuration (&(*ctxt)->cfg, pilot_id);
}

static void
e_calendar_context_destroy (ECalConduitContext **ctxt)
{
	g_return_if_fail (ctxt!=NULL);
	g_return_if_fail (*ctxt!=NULL);

	if ((*ctxt)->client != NULL)
		gtk_object_unref (GTK_OBJECT ((*ctxt)->client));

	if ((*ctxt)->cfg != NULL)
		calconduit_destroy_configuration (&(*ctxt)->cfg);

	g_free (*ctxt);
	*ctxt = NULL;
}

/* Calendar Server routines */
static void
start_calendar_server_cb (GtkWidget *cal_client,
			  CalClientLoadStatus status,
			  ECalConduitContext *ctxt)
{
	CalClient *client = CAL_CLIENT (cal_client);

	LOG ("  entering start_calendar_server_load_cb, tried=%d\n",
		ctxt->calendar_load_tried);

	if (status == CAL_CLIENT_LOAD_SUCCESS) {
		ctxt->calendar_load_success = TRUE;
		LOG ("    success\n");
		gtk_main_quit (); /* end the sub event loop */
	} else {
		if (ctxt->calendar_load_tried) {
			LOG ("    load and create of calendar failed\n");
			gtk_main_quit (); /* end the sub event loop */
			return;
		}

		cal_client_create_calendar (client, ctxt->calendar_file);
		ctxt->calendar_load_tried = TRUE;
	}
}

static int
start_calendar_server (ECalConduitContext *ctxt)
{
	
	g_return_val_if_fail (ctxt != NULL, -2);

	ctxt->client = cal_client_new ();

	/* FIX ME */
	ctxt->calendar_file = g_concat_dir_and_file (g_get_home_dir (),
			       "evolution/local/Calendar/calendar.ics");

	gtk_signal_connect (GTK_OBJECT (ctxt->client), "cal_loaded",
			    start_calendar_server_cb, ctxt);

	LOG ("    calling cal_client_load_calendar\n");
	cal_client_load_calendar (ctxt->client, ctxt->calendar_file);

	/* run a sub event loop to turn cal-client's async load
	   notification into a synchronous call */
	gtk_main ();

	if (ctxt->calendar_load_success)
		return 0;

	return -1;
}

/* Utility routines */
static char *
map_name (ECalConduitContext *ctxt) 
{
	char *filename;
	
	filename = g_strdup_printf ("%s/evolution/local/Calendar/pilot-map-%d.xml", g_get_home_dir (), ctxt->cfg->pilot_id);

	return filename;
}

static icalrecurrencetype_weekday
get_ical_day (int day) 
{
	switch (day) {
	case 0:
		return ICAL_MONDAY_WEEKDAY;
	case 1:
		return ICAL_TUESDAY_WEEKDAY;
	case 2:
		return ICAL_WEDNESDAY_WEEKDAY;
	case 3:
		return ICAL_THURSDAY_WEEKDAY;
	case 4:
		return ICAL_FRIDAY_WEEKDAY;
	case 5:
		return ICAL_SATURDAY_WEEKDAY;
	case 6:
		return ICAL_SUNDAY_WEEKDAY;
	}

	return ICAL_NO_WEEKDAY;
}

static void
compute_pid (ECalConduitContext *ctxt, ECalLocalRecord *local, const char *uid)
{
	guint32 *pid;
	
	pid = g_hash_table_lookup (ctxt->uid_map, uid);
	
	if (pid)
		local->local.ID = *pid;
	else
		local->local.ID = 0;
}

static void
compute_status (ECalConduitContext *ctxt, ECalLocalRecord *local, const char *uid)
{
	local->local.archived = FALSE;
	local->local.secret = FALSE;
	
	if (g_hash_table_lookup (ctxt->added, uid))
		local->local.attr = GnomePilotRecordNew;
	else if (g_hash_table_lookup (ctxt->modified, uid))
		local->local.attr = GnomePilotRecordModified;
	else if (g_hash_table_lookup (ctxt->deleted, uid))
		local->local.attr = GnomePilotRecordDeleted;
	else
		local->local.attr = GnomePilotRecordNothing;
}

static GnomePilotRecord *
local_record_to_pilot_record (ECalLocalRecord *local,
			      ECalConduitContext *ctxt)
{
	GnomePilotRecord *p = NULL;
	
	g_return_val_if_fail (local != NULL, NULL);
	g_assert (local->comp != NULL);
	g_assert (local->appt != NULL );
	
	LOG ("local_record_to_remote_record\n");

	p = g_new0 (GnomePilotRecord, 1);

	p->ID = local->local.ID;
	p->category = 0;
	p->attr = local->local.attr;
	p->archived = local->local.archived;
	p->secret = local->local.secret;

	/* Generate pilot record structure */
	p->record = g_new0 (char,0xffff);
	p->length = pack_Appointment (local->appt, p->record, 0xffff);

	return p;	
}

/*
 * converts a CalComponent object to a ECalLocalRecord
 */
static void
local_record_from_comp (ECalLocalRecord *local, CalComponent *comp, ECalConduitContext *ctxt) 
{
	const char *uid;
	CalComponentText summary;
	GSList *d_list = NULL;
	CalComponentText *description;
	CalComponentDateTime dt;
	time_t dt_time;
	CalComponentClassification classif;
	int i;
	
	LOG ("local_record_from_comp\n");

	g_return_if_fail (local != NULL);
	g_return_if_fail (comp != NULL);

	local->comp = comp;

	cal_component_get_uid (local->comp, &uid);
	compute_pid (ctxt, local, uid);
	compute_status (ctxt, local, uid);

	local->appt = g_new0 (struct Appointment,1);

	/* STOP: don't replace these with g_strdup, since free_Appointment
	   uses free to deallocate */
	cal_component_get_summary (comp, &summary);
	if (summary.value) 
		local->appt->description = strdup ((char *) summary.value);

	cal_component_get_description_list (comp, &d_list);
	if (d_list) {
		description = (CalComponentText *) d_list->data;
		if (description && description->value)
			local->appt->note = strdup (description->value);
		else
			local->appt->note = NULL;
	} else {
		local->appt->note = NULL;
	}

	cal_component_get_dtstart (comp, &dt);	
	if (dt.value) {
		dt_time = icaltime_as_timet (*dt.value);
		
		local->appt->begin = *localtime (&dt_time);
	}

	cal_component_get_dtend (comp, &dt);	
	if (dt.value) {
		dt_time = icaltime_as_timet (*dt.value);
		
		local->appt->end = *localtime (&dt_time);
		local->appt->event = 0;
	} else {
		local->appt->event = 1;
	}

	/* Recurrence Rules */
	local->appt->repeatType = repeatNone;

	if (cal_component_has_rrules (comp)) {
		GSList *list;
		struct icalrecurrencetype *recur;
		
		cal_component_get_rrule_list (comp, &list);
		recur = list->data;
		
		switch (recur->freq) {
		case ICAL_DAILY_RECURRENCE:
			local->appt->repeatType = repeatDaily;
			break;
		case ICAL_WEEKLY_RECURRENCE:
			local->appt->repeatType = repeatWeekly;
			for (i = 0; i<= 7 && recur->by_day[i] != SHRT_MAX; i++)
				local->appt->repeatDays[0] = 0;
			break;
		case ICAL_MONTHLY_RECURRENCE:
			if (recur->by_month_day[0] != SHRT_MAX) {
				local->appt->repeatType = repeatMonthlyByDate;
			}
			break;
		case ICAL_YEARLY_RECURRENCE:
			local->appt->repeatType = repeatYearly;
			break;
		default:
			break;
		}

		if (local->appt->repeatType != repeatNone) {
			local->appt->repeatFrequency = recur->interval;
		}
		
		cal_component_free_recur_list (list);
	}

	cal_component_get_classification (comp, &classif);

	if (classif == CAL_COMPONENT_CLASS_PRIVATE)
		local->local.secret = 1;
	else
		local->local.secret = 0;

	local->local.archived = 0;  
}

static void 
local_record_from_uid (ECalLocalRecord *local,
		       char *uid,
		       ECalConduitContext *ctxt)
{
	CalComponent *comp;
	CalClientGetStatus status;

	g_assert(local!=NULL);

	status = cal_client_get_object (ctxt->client, uid, &comp);

	if (status == CAL_CLIENT_GET_SUCCESS) {
		local_record_from_comp (local, comp, ctxt);
	} else if (status == CAL_CLIENT_GET_NOT_FOUND) {
		comp = cal_component_new ();
		cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);
		cal_component_set_uid (comp, uid);
		local_record_from_comp (local, comp, ctxt);
	} else {
		INFO ("Object did not exist");
	}	
}

static CalComponent *
comp_from_remote_record (GnomePilotConduitSyncAbs *conduit,
			 GnomePilotRecord *remote,
			 CalComponent *in_comp)
{
	CalComponent *comp;
	struct Appointment appt;
	struct icaltimetype now = icaltime_from_timet (time (NULL), FALSE, FALSE), it;
	struct icalrecurrencetype recur;
	int pos, i;
	CalComponentText summary = {NULL, NULL};
	CalComponentText description = {NULL, NULL};
	CalComponentDateTime dt = {NULL, NULL};
 	GSList *d_list;

	g_return_val_if_fail (remote != NULL, NULL);

	memset (&appt, 0, sizeof (struct Appointment));
	unpack_Appointment (&appt, remote->record, remote->length);

	if (in_comp == NULL) {
		comp = cal_component_new ();
		cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);
		cal_component_set_created (comp, &now);
	} else {
		comp = cal_component_clone (in_comp);
	}

 	LOG ("        comp_from_remote_record: "
 	     "creating from remote %s and comp %s\n", 
  	     print_remote (remote), cal_component_get_as_string (comp));

	cal_component_set_last_modified (comp, &now);

	summary.value = appt.description;
	cal_component_set_summary (comp, &summary);

	description.value = appt.note;
	d_list = g_slist_append (NULL, &description);
	cal_component_set_comment_list (comp, d_list);
	g_slist_free (d_list);


	/* FIX ME This is a bit hackish, how else can we tell if there is
	 * no due date set?
	 */
	if (appt.begin.tm_sec || appt.begin.tm_min || appt.begin.tm_hour 
	    || appt.begin.tm_mday || appt.begin.tm_mon || appt.begin.tm_year) {
		it = icaltime_from_timet (mktime (&appt.begin), FALSE, FALSE);
		dt.value = &it;
		cal_component_set_dtstart (comp, &dt);
	}

	if (appt.end.tm_sec || appt.end.tm_min || appt.end.tm_hour 
	    || appt.end.tm_mday || appt.end.tm_mon || appt.end.tm_year) {
		it = icaltime_from_timet (mktime (&appt.end), FALSE, FALSE);
		dt.value = &it;
		cal_component_set_dtend (comp, &dt);
	}

	/* Recurrence information */
  	icalrecurrencetype_clear (&recur);

	switch (appt.repeatType) {
	case repeatNone:
		recur.freq = ICAL_NO_RECURRENCE;
		/* nothing */
		break;

	case repeatDaily:
		recur.freq = ICAL_DAILY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		break;

	case repeatWeekly:
		recur.freq = ICAL_WEEKLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;

		pos = 0;
		for (i = 0; i < 7; i++) {
			if (appt.repeatDays[i])
				recur.by_day[pos++] = get_ical_day (i);
		}
		
		break;

	case repeatMonthlyByDay:
		recur.freq = ICAL_MONTHLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		recur.by_month_day[0] = appt.begin.tm_mday;
		break;
		
	case repeatMonthlyByDate:
		recur.freq = ICAL_MONTHLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		/* Not handled! */
		break;

	case repeatYearly:
		recur.freq = ICAL_YEARLY_RECURRENCE;
		recur.interval = appt.repeatFrequency;
		break;
		
	default:
		g_assert_not_reached ();
	}

	if (recur.freq != ICAL_NO_RECURRENCE) {
		GSList *list = NULL;
		
		/* recurrence start of week */
		recur.week_start = get_ical_day (appt.repeatWeekstart);

		if (appt.repeatEnd.tm_sec || appt.repeatEnd.tm_min || appt.repeatEnd.tm_hour 
		    || appt.repeatEnd.tm_mday || appt.repeatEnd.tm_mon || appt.repeatEnd.tm_year) {
			time_t t = mktime (&appt.repeatEnd);
			t = time_add_day (t, 1);
			recur.until = icaltime_from_timet (t, FALSE, FALSE);
		}

		list = g_slist_append (list, &recur);
		cal_component_set_rrule_list (comp, list);
		g_slist_free (list);
	} else {
		cal_component_set_rrule_list (comp, NULL);		
	}
	
	cal_component_set_transparency (comp, CAL_COMPONENT_TRANSP_NONE);

	if (remote->attr & dlpRecAttrSecret)
		cal_component_set_classification (comp, CAL_COMPONENT_CLASS_PRIVATE);
	else
		cal_component_set_classification (comp, CAL_COMPONENT_CLASS_PUBLIC);

	cal_component_commit_sequence (comp);
	
	free_Appointment (&appt);

	return comp;
}

static void
update_comp (GnomePilotConduitSyncAbs *conduit, CalComponent *comp,
	     ECalConduitContext *ctxt) 
{
	gboolean success;

	g_return_if_fail (conduit != NULL);
	g_return_if_fail (comp != NULL);

	LOG ("update_comp: saving to desktop\n%s\n", 
	     cal_component_get_as_string (comp));

	success = cal_client_update_object (ctxt->client, comp);

	if (!success)
		WARN (_("Error while communicating with calendar server"));
}

static void
check_for_slow_setting (GnomePilotConduit *c, ECalConduitContext *ctxt)
{
	int count, map_count;

	count = g_list_length (ctxt->uids);
	map_count = g_hash_table_size (ctxt->pid_map);
	
	/* If there are no objects or objects but no log */
	if ((count == 0) || (count > 0 && map_count == 0)) {
		GnomePilotConduitStandard *conduit;
		LOG ("    doing slow sync\n");
		conduit = GNOME_PILOT_CONDUIT_STANDARD (c);
		gnome_pilot_conduit_standard_set_slow (conduit);
	} else {
		LOG ("    doing fast sync\n");
	}
}

/* Pilot syncing callbacks */
static gint
pre_sync (GnomePilotConduit *conduit,
	  GnomePilotDBInfo *dbi,
	  ECalConduitContext *ctxt)
{
	GnomePilotConduitSyncAbs *abs_conduit;
	GList *l;
	int len;
	unsigned char *buf;
	char *filename;
	xmlSAXHandler handler;
	gint num_records;

	abs_conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit);

	LOG ("---------------------------------------------------------\n");
	LOG ("pre_sync: Calendar Conduit v.%s", CONDUIT_VERSION);
	g_message ("Calendar Conduit v.%s", CONDUIT_VERSION);

	ctxt->client = NULL;
	
	if (start_calendar_server (ctxt) != 0) {
		WARN(_("Could not start wombat server"));
		gnome_pilot_conduit_error (conduit, _("Could not start wombat"));
		return -1;
	}

	/* Get the local database */
	ctxt->uids = cal_client_get_uids (ctxt->client, CALOBJ_TYPE_EVENT);
	
	/* Load the uid <--> pilot id mapping */
	ctxt->pid_map = g_hash_table_new (g_int_hash, g_int_equal);
	ctxt->uid_map = g_hash_table_new (g_str_hash, g_str_equal);

	filename = map_name (ctxt);
	e_pilot_map_read (filename, ctxt->pid_map, ctxt->uid_map, &ctxt->since);
	g_free (filename);

	/* Find the added, modified and deleted items */
	ctxt->added = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt->modified = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt->deleted = g_hash_table_new (g_str_hash, g_str_equal);

	ctxt->changed = cal_client_get_changed_uids (ctxt->client, 
						     CALOBJ_TYPE_EVENT,
						     ctxt->since + 1);
	for (l = ctxt->changed; l != NULL; l = l->next) {
		CalObjChange *coc = l->data;
		
		switch (coc->type) {
		case CALOBJ_UPDATED:
			if (g_hash_table_lookup (ctxt->uid_map, coc->uid))
				g_hash_table_insert (ctxt->modified, coc->uid, coc);
			else
				g_hash_table_insert (ctxt->added, coc->uid, coc);
			break;
		case CALOBJ_REMOVED:
			g_hash_table_insert (ctxt->deleted, coc->uid, coc);
			break;
		}
	}

	/* Set the count information */
	num_records = cal_client_get_n_objects (ctxt->client, CALOBJ_TYPE_EVENT);
	gnome_pilot_conduit_sync_abs_set_num_local_records(abs_conduit, num_records);
	num_records = g_hash_table_size (ctxt->added);
	gnome_pilot_conduit_sync_abs_set_num_new_local_records (abs_conduit, num_records);
	num_records = g_hash_table_size (ctxt->modified);
	gnome_pilot_conduit_sync_abs_set_num_updated_local_records (abs_conduit, num_records);
	num_records = g_hash_table_size (ctxt->deleted);
	gnome_pilot_conduit_sync_abs_set_num_deleted_local_records(abs_conduit, num_records);

	gtk_object_set_data (GTK_OBJECT (conduit), "dbinfo", dbi);

	buf = (unsigned char*)g_malloc (0xffff);
	len = dlp_ReadAppBlock (dbi->pilot_socket, dbi->db_handle, 0,
			      (unsigned char *)buf, 0xffff);
	
	if (len < 0) {
		WARN (_("Could not read pilot's Calendar application block"));
		WARN ("dlp_ReadAppBlock(...) = %d", len);
		gnome_pilot_conduit_error (conduit,
					   _("Could not read pilot's Calendar application block"));
		return -1;
	}
	unpack_AppointmentAppInfo (&(ctxt->ai), buf, len);
	g_free (buf);

	check_for_slow_setting (conduit, ctxt);

	return 0;
}

static gint
post_sync (GnomePilotConduit *conduit,
	   GnomePilotDBInfo *dbi,
	   ECalConduitContext *ctxt)
{
	gchar *filename;
	
	LOG ("post_sync: Calendar Conduit v.%s", CONDUIT_VERSION);
	LOG ("---------------------------------------------------------\n");

	filename = map_name (ctxt);
	e_pilot_map_write (filename, ctxt->pid_map);
	g_free (filename);
	
	return 0;
}

static gint
set_pilot_id (GnomePilotConduitSyncAbs *conduit,
	      ECalLocalRecord *local,
	      guint32 ID,
	      ECalConduitContext *ctxt)
{
	const char *uid;
	char *new_uid;
	guint32 *pid = g_new (guint32, 1);

	LOG ("set_pilot_id: setting to %d\n", ID);
	
	cal_component_get_uid (local->comp, &uid);
	*pid = ID;
	new_uid = g_strdup (uid);
	g_hash_table_insert (ctxt->pid_map, pid, new_uid);
	g_hash_table_insert (ctxt->uid_map, new_uid, pid);

        return 0;
}

static gint
for_each (GnomePilotConduitSyncAbs *conduit,
	  ECalLocalRecord **local,
	  ECalConduitContext *ctxt)
{
	static GList *uids, *iterator;
	static int count;

	g_return_val_if_fail (local != NULL, -1);

	if (*local == NULL) {
		LOG ("beginning for_each");

		uids = ctxt->uids;
		count = 0;
		
		if (uids != NULL) {
			LOG ("iterating over %d records", g_list_length (uids));

			*local = g_new0 (ECalLocalRecord, 1);
			local_record_from_uid (*local, uids->data, ctxt);

			iterator = uids;
		} else {
			LOG ("no events");
			(*local) = NULL;
			return 0;
		}
	} else {
		count++;
		if (g_list_next (iterator)) {
			iterator = g_list_next (iterator);

			*local = g_new0 (ECalLocalRecord, 1);
			local_record_from_uid (*local, iterator->data, ctxt);
		} else {
			LOG ("for_each ending");

			/* Tell the pilot the iteration is over */
			*local = NULL;

			return 0;
		}
	}

	return 0;
}

static gint
for_each_modified (GnomePilotConduitSyncAbs *conduit,
		   ECalLocalRecord **local,
		   ECalConduitContext *ctxt)
{
	static GList *changes, *iterator;
	static int count;

	g_return_val_if_fail (local != NULL, 0);

	if (*local == NULL) {
		LOG ("beginning for_each_modified: beginning\n");
		
		changes = ctxt->changed;
		
		count = 0;
		
		if (changes != NULL) {
			CalObjChange *coc = changes->data;
			
			LOG ("iterating over %d records", g_list_length (changes));
			 
			*local = g_new0 (ECalLocalRecord, 1);
			local_record_from_uid (*local, coc->uid, ctxt);

			iterator = changes;
		} else {
			LOG ("no events");
			(*local) = NULL;
			return 0;
		}
	} else {
		count++;
		if (g_list_next (iterator)) {
			CalObjChange *coc;

			iterator = g_list_next (iterator);
			coc = iterator->data;

			*local = g_new0 (ECalLocalRecord, 1);
			local_record_from_uid (*local, coc->uid, ctxt);
		} else {
			LOG ("for_each_modified ending");

			/* Tell the pilot the iteration is over */
			(*local) = NULL;

			return 0;
		}
	}

	return 0;
}

static gint
compare (GnomePilotConduitSyncAbs *conduit,
	 ECalLocalRecord *local,
	 GnomePilotRecord *remote,
	 ECalConduitContext *ctxt)
{
	/* used by the quick compare */
	GnomePilotRecord *local_pilot;
	int retval = 0;

	LOG ("compare: local=%s remote=%s...\n",
		print_local (local), print_remote (remote));

	g_return_val_if_fail (local!=NULL,-1);
	g_return_val_if_fail (remote!=NULL,-1);

	local_pilot = local_record_to_pilot_record (local, ctxt);
	if (!local_pilot) 
		return -1;

	if (remote->length != local_pilot->length
	    || memcmp (local_pilot->record, remote->record, remote->length))
		retval = 1;

	if (retval == 0)
		LOG ("    equal");
	else
		LOG ("    not equal");
	
	g_free (local_pilot);
	
	return retval;
}

static gint
add_record (GnomePilotConduitSyncAbs *conduit,
	    GnomePilotRecord *remote,
	    ECalConduitContext *ctxt)
{
	CalComponent *comp;
	const char *uid;
	char *new_uid;
	guint32 *pid = g_new (guint32, 1);
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1);

	LOG ("add_record: adding %s to desktop\n", print_remote (remote));

	comp = comp_from_remote_record (conduit, remote, NULL);
	update_comp (conduit, comp, ctxt);

	*pid = remote->ID;
	cal_component_get_uid (comp, &uid);
	new_uid = g_strdup (uid);
	g_hash_table_insert (ctxt->pid_map, pid, new_uid);
	g_hash_table_insert (ctxt->uid_map, new_uid, pid);

	return retval;
}

static gint
add_archive_record (GnomePilotConduitSyncAbs *conduit,
		    ECalLocalRecord *local,
		    GnomePilotRecord *remote,
		    ECalConduitContext *ctxt)
{
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1); 
	g_return_val_if_fail (local != NULL, -1);

	LOG ("add_archive_record: doing nothing with %s\n",
	     print_local (local));

	return retval;
}

static gint
replace_record (GnomePilotConduitSyncAbs *conduit,
		ECalLocalRecord *local,
		GnomePilotRecord *remote,
		ECalConduitContext *ctxt)
{
	CalComponent *new_comp;
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1);

	LOG ("replace_record: replace %s with %s\n",
	     print_local (local), print_remote (remote));

	new_comp = comp_from_remote_record (conduit, remote, local->comp);
	gtk_object_unref (GTK_OBJECT (local->comp));
	local->comp = new_comp;
	update_comp (conduit, local->comp, ctxt);

	gtk_object_unref (GTK_OBJECT (new_comp));

	return retval;
}

static gint
delete_record (GnomePilotConduitSyncAbs *conduit,
	       ECalLocalRecord *local,
	       ECalConduitContext *ctxt)
{
	const char *uid;

	g_return_val_if_fail (local != NULL, -1);
	g_assert (local->comp != NULL);

	cal_component_get_uid (local->comp, &uid);

	LOG ("delete_record: deleting %s\n", uid);

	cal_client_remove_object (ctxt->client, uid);
	
        return 0;
}

static gint
delete_archive_record (GnomePilotConduitSyncAbs *conduit,
		       ECalLocalRecord *local,
		       ECalConduitContext *ctxt)
{
	int retval = 0;
	
	g_return_val_if_fail(local!=NULL,-1);

	LOG ("delete_archive_record: doing nothing\n");

        return retval;
}

static gint
match (GnomePilotConduitSyncAbs *conduit,
       GnomePilotRecord *remote,
       ECalLocalRecord **local,
       ECalConduitContext *ctxt)
{
	char *uid;
	
	LOG ("match: looking for local copy of %s\n",
	     print_remote (remote));	
	
	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (remote != NULL, -1);

	*local = NULL;
	uid = g_hash_table_lookup (ctxt->pid_map, &remote->ID);
	
	if (!uid)
		return 0;

	LOG ("  matched\n");
	
	*local = g_new0 (ECalLocalRecord, 1);
	local_record_from_uid (*local, uid, ctxt);
	
	return 0;
}

static gint
free_match (GnomePilotConduitSyncAbs *conduit,
	    ECalLocalRecord *local,
	    ECalConduitContext *ctxt)
{
	LOG ("free_match: freeing\n");

	g_return_val_if_fail (local != NULL, -1);

	gtk_object_unref (GTK_OBJECT (local->comp));
	g_free (local);

	return 0;
}

static gint
prepare (GnomePilotConduitSyncAbs *conduit,
	 ECalLocalRecord *local,
	 GnomePilotRecord **remote,
	 ECalConduitContext *ctxt)
{
	LOG ("prepare: encoding local %s\n", print_local (local));

	*remote = local_record_to_pilot_record (local, ctxt);

	if (!*remote)
		return -1;
	
	return 0;
}

static gint
free_prepare (GnomePilotConduitSyncAbs *conduit,
	      ECalLocalRecord *local,
	      GnomePilotRecord **remote,
	      ECalConduitContext *ctxt)
{
	LOG ("free_prepare: freeing\n");

	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (remote != NULL, -1);

	g_free (*remote);
	*remote = NULL;

        return 0;
}

static ORBit_MessageValidationResult
accept_all_cookies (CORBA_unsigned_long request_id,
		    CORBA_Principal *principal,
		    CORBA_char *operation)
{
	/* allow ALL cookies */
	return ORBIT_MESSAGE_ALLOW_ALL;
}


GnomePilotConduit *
conduit_get_gpilot_conduit (guint32 pilot_id)
{
	GtkObject *retval;
	ECalConduitContext *ctxt;

	LOG ("in calendar's conduit_get_gpilot_conduit\n");

	/* we need to find wombat with oaf, so make sure oaf
	   is initialized here.  once the desktop is converted
	   to oaf and gpilotd is built with oaf, this can go away */
	if (!oaf_is_initialized ()) {
		char *argv[ 1 ] = {"hi"};
		oaf_init (1, argv);

		if (bonobo_init (CORBA_OBJECT_NIL,
				 CORBA_OBJECT_NIL,
				 CORBA_OBJECT_NIL) == FALSE)
			g_error (_("Could not initialize Bonobo"));

		ORBit_set_request_validation_handler (accept_all_cookies);
	}

	retval = gnome_pilot_conduit_sync_abs_new ("DatebookDB", 0x64617465);
	g_assert (retval != NULL);

	gnome_pilot_conduit_construct (GNOME_PILOT_CONDUIT (retval),
				       "e_calendar_conduit");

	e_calendar_context_new (&ctxt, pilot_id);
	gtk_object_set_data (GTK_OBJECT (retval), "calconduit_context", ctxt);

	gtk_signal_connect (retval, "pre_sync", (GtkSignalFunc) pre_sync, ctxt);
	gtk_signal_connect (retval, "post_sync", (GtkSignalFunc) post_sync, ctxt);

  	gtk_signal_connect (retval, "set_pilot_id", (GtkSignalFunc) set_pilot_id, ctxt);

  	gtk_signal_connect (retval, "for_each", (GtkSignalFunc) for_each, ctxt);
  	gtk_signal_connect (retval, "for_each_modified", (GtkSignalFunc) for_each_modified, ctxt);
  	gtk_signal_connect (retval, "compare", (GtkSignalFunc) compare, ctxt);

  	gtk_signal_connect (retval, "add_record", (GtkSignalFunc) add_record, ctxt);
  	gtk_signal_connect (retval, "add_archive_record", (GtkSignalFunc) add_archive_record, ctxt);

  	gtk_signal_connect (retval, "replace_record", (GtkSignalFunc) replace_record, ctxt);

  	gtk_signal_connect (retval, "delete_record", (GtkSignalFunc) delete_record, ctxt);
  	gtk_signal_connect (retval, "delete_archive_record", (GtkSignalFunc) delete_archive_record, ctxt);

  	gtk_signal_connect (retval, "match", (GtkSignalFunc) match, ctxt);
  	gtk_signal_connect (retval, "free_match", (GtkSignalFunc) free_match, ctxt);

  	gtk_signal_connect (retval, "prepare", (GtkSignalFunc) prepare, ctxt);
  	gtk_signal_connect (retval, "free_prepare", (GtkSignalFunc) free_prepare, ctxt);

	return GNOME_PILOT_CONDUIT (retval);
}

void
conduit_destroy_gpilot_conduit (GnomePilotConduit *conduit)
{ 
	ECalConduitContext *ctxt;

	ctxt = gtk_object_get_data (GTK_OBJECT (conduit), 
				    "calconduit_context");

	e_calendar_context_destroy (&ctxt);

	gtk_object_destroy (GTK_OBJECT (conduit));
}
