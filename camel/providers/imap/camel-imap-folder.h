/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.h : Abstract class for an imap folder */

/* 
 * Author: 
 *   Jeffrey Stedfast <fejj@helixcode.com> 
 *
 * Copyright (C) 2000 Helix Code, Inc. (www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_IMAP_FOLDER_H
#define CAMEL_IMAP_FOLDER_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-imap-types.h"
#include "camel-folder.h"
#include <camel/camel-folder-search.h>

#define CAMEL_IMAP_FOLDER_TYPE     (camel_imap_folder_get_type ())
#define CAMEL_IMAP_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_FOLDER_TYPE, CamelImapFolder))
#define CAMEL_IMAP_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_FOLDER_TYPE, CamelImapFolderClass))
#define CAMEL_IS_IMAP_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_FOLDER_TYPE))

struct _CamelImapFolder {
	CamelFolder parent_object;

	struct _CamelImapFolderPrivate *priv;

	CamelFolderSearch *search;
	CamelImapMessageCache *cache;
};


typedef struct {
	CamelFolderClass parent_class;

	/* Virtual methods */	
	
} CamelImapFolderClass;


/* public methods */
CamelFolder *camel_imap_folder_new (CamelStore *parent,
				    const char *folder_name,
				    const char *short_name,
				    const char *folder_dir,
				    CamelException *ex);

void camel_imap_folder_selected (CamelFolder *folder,
				 CamelImapResponse *response,
				 CamelException *ex);

void camel_imap_folder_changed (CamelFolder *folder, int exists,
				GArray *expunged, CamelException *ex);

CamelStream *camel_imap_folder_fetch_data (CamelImapFolder *imap_folder,
					   const char *uid,
					   const char *section_text,
					   gboolean cache_only,
					   CamelException *ex);

/* Standard Camel function */
CamelType camel_imap_folder_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_IMAP_FOLDER_H */
