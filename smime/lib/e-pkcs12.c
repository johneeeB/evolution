/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-pkcs12.c
 *
 * Copyright (C) 2003 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Chris Toshok (toshok@ximian.com)
 */

/* The following is the mozilla license blurb, as the bodies some of
   these functions were derived from the mozilla source. */

/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Netscape security libraries.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 2000 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable 
 * instead of those above.  If you wish to allow use of your 
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 *
 */

#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>

#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "e-util/e-passwords.h"
#include "e-pkcs12.h"

#include "prmem.h"
#include "nss.h"
#include "pkcs12.h"
#include "p12plcy.h"
#include "pk11func.h"
#include "secerr.h"

struct _EPKCS12Private {
	int tmp_fd;
	char *tmp_path;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

// static callback functions for the NSS PKCS#12 library
static SECItem * PR_CALLBACK nickname_collision(SECItem *, PRBool *, void *);
static void      PR_CALLBACK write_export_file(void *arg, const char *buf, unsigned long len);

static gboolean handle_error(int myerr);

#define PKCS12_TMPFILENAME         ".p12tmp"
#define PKCS12_BUFFER_SIZE         2048
#define PKCS12_RESTORE_OK          1
#define PKCS12_BACKUP_OK           2
#define PKCS12_USER_CANCELED       3
#define PKCS12_NOSMARTCARD_EXPORT  4
#define PKCS12_RESTORE_FAILED      5
#define PKCS12_BACKUP_FAILED       6
#define PKCS12_NSS_ERROR           7

static void
e_pkcs12_dispose (GObject *object)
{
	EPKCS12 *pk = E_PKCS12 (object);

	if (!pk->priv)
		return;

	/* XXX free instance private foo */

	g_free (pk->priv);
	pk->priv = NULL;
	
	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_pkcs12_class_init (EPKCS12Class *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->dispose = e_pkcs12_dispose;
}

static void
e_pkcs12_init (EPKCS12 *ec)
{
	ec->priv = g_new0 (EPKCS12Private, 1);
}

GType
e_pkcs12_get_type (void)
{
	static GType pkcs12_type = 0;

	if (!pkcs12_type) {
		static const GTypeInfo pkcs12_info =  {
			sizeof (EPKCS12Class),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) e_pkcs12_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EPKCS12),
			0,             /* n_preallocs */
			(GInstanceInitFunc) e_pkcs12_init,
		};

		pkcs12_type = g_type_register_static (PARENT_TYPE, "EPKCS12", &pkcs12_info, 0);
	}

	return pkcs12_type;
}



EPKCS12*
e_pkcs12_new (void)
{
	EPKCS12 *pk = E_PKCS12 (g_object_new (E_TYPE_PKCS12, NULL));

	return pk;
}

static gboolean
input_to_decoder (SEC_PKCS12DecoderContext *dcx, const char *path, GError **error)
{
	/*  nsNSSShutDownPreventionLock locker; */
	SECStatus srv;
	int amount;
	char buf[PKCS12_BUFFER_SIZE];
	FILE *fp;

	/* open path */
	fp = fopen (path, "r");
	if (!fp) {
		/* XXX gerror */
		printf ("couldn't open `%s'\n", path);
		return FALSE;
	}

	while (TRUE) {
		amount = fread (buf, 1, sizeof (buf), fp);
		if (amount < 0) {
			printf ("got -1 fread\n");
			fclose (fp);
			return FALSE;
		}
		/* feed the file data into the decoder */
		srv = SEC_PKCS12DecoderUpdate(dcx, 
					      (unsigned char*) buf, 
					      amount);
		if (srv) {
			/* don't allow the close call to overwrite our precious error code */
			/* XXX g_error */
			int pr_err = PORT_GetError();
			PORT_SetError(pr_err);
			printf ("SEC_PKCS12DecoderUpdate returned %d\n", srv);
			fclose (fp);
			return FALSE;
		}
		if (amount < PKCS12_BUFFER_SIZE)
			break;
	}
	fclose (fp);
	return TRUE;
}

static gboolean
prompt_for_password (char *title, char *prompt, SECItem *pwd)
{
	char *passwd;

	passwd = e_passwords_ask_password (title, NULL, NULL, prompt, TRUE,
					   E_PASSWORDS_DO_NOT_REMEMBER, NULL,
					   NULL);

	if (passwd) {
		SECITEM_AllocItem(NULL, pwd, PL_strlen (passwd));
		memcpy (pwd->data, passwd, strlen (passwd));
		g_free (passwd);
	}

	return TRUE;
}

static gboolean
import_from_file_helper (EPKCS12 *pkcs12, const char *path, gboolean *aWantRetry, GError **error)
{
	/*nsNSSShutDownPreventionLock locker; */
	gboolean rv = TRUE;
	SECStatus srv = SECSuccess;
	SEC_PKCS12DecoderContext *dcx = NULL;
	SECItem passwd;
	GError *err = NULL;
	PK11SlotInfo *slot = PK11_GetInternalKeySlot (); /* XXX toshok - we
							    hardcode this
							    here */
	*aWantRetry = FALSE;


	passwd.data = NULL;
	rv = prompt_for_password (_("PKCS12 File Password"), _("Enter password for PKCS12 file:"), &passwd);
	if (!rv) goto finish;
	if (passwd.data == NULL) {
		handle_error (PKCS12_USER_CANCELED);
		return TRUE;
	}

#if notyet
	/* XXX we don't need this block as long as we hardcode the
	   slot above */
	nsXPIDLString tokenName;
	nsXPIDLCString tokenNameCString;
	const char *tokNameRef;
  

	mToken->GetTokenName (getter_Copies(tokenName));
	tokenNameCString.Adopt (ToNewUTF8String(tokenName));
	tokNameRef = tokenNameCString; /* I do this here so that the
					  NS_CONST_CAST below doesn't
					  break the build on Win32 */

	slot = PK11_FindSlotByName (NS_CONST_CAST(char*,tokNameRef));
	if (!slot) {
		srv = SECFailure;
		goto finish;
	}
#endif

	/* initialize the decoder */
	dcx = SEC_PKCS12DecoderStart (&passwd, slot, NULL,
				      NULL, NULL,
				      NULL, NULL,
				      pkcs12);
	if (!dcx) {
		srv = SECFailure;
		goto finish;
	}
	/* read input file and feed it to the decoder */
	rv = input_to_decoder (dcx, path, &err);
	if (!rv) {
#if notyet
		/* XXX we need this to check the gerror */
		if (NS_ERROR_ABORT == rv) {
			// inputToDecoder indicated a NSS error
			srv = SECFailure;
		}
#endif
		goto finish;
	}

	/* verify the blob */
	srv = SEC_PKCS12DecoderVerify (dcx);
	if (srv) { printf ("decoderverify failed\n"); goto finish; }
	/* validate bags */
	srv = SEC_PKCS12DecoderValidateBags (dcx, nickname_collision);
	if (srv) { printf ("decodervalidatebags failed\n"); goto finish; }
	/* import cert and key */
	srv = SEC_PKCS12DecoderImportBags (dcx);
	if (srv) { printf ("decoderimportbags failed\n"); goto finish; }
	/* Later - check to see if this should become default email cert */
	handle_error (PKCS12_RESTORE_OK);
 finish:
	/* If srv != SECSuccess, NSS probably set a specific error code.
	   We should use that error code instead of inventing a new one
	   for every error possible. */
	if (srv != SECSuccess) {
		printf ("srv != SECSuccess\n");
		if (SEC_ERROR_BAD_PASSWORD == PORT_GetError()) {
			printf ("BAD PASSWORD\n");
			*aWantRetry = TRUE;
		}
		handle_error(PKCS12_NSS_ERROR);
	} else if (!rv) {
		handle_error(PKCS12_RESTORE_FAILED);
	}
	if (slot)
		PK11_FreeSlot(slot);
	// finish the decoder
	if (dcx)
		SEC_PKCS12DecoderFinish(dcx);
	return TRUE;
}

gboolean
e_pkcs12_import_from_file (EPKCS12 *pkcs12, const char *path, GError **error)
{
	/*nsNSSShutDownPreventionLock locker;*/
	gboolean rv = TRUE;
	gboolean wantRetry;
  

#if 0
	/* XXX we don't use tokens yet */
	if (!mToken) {
		if (!mTokenSet) {
			rv = SetToken(NULL); // Ask the user to pick a slot
			if (NS_FAILED(rv)) {
				handle_error(PKCS12_USER_CANCELED);
				return rv;
			}
		}
	}

	if (!mToken) {
		handle_error(PKCS12_RESTORE_FAILED);
		return NS_ERROR_NOT_AVAILABLE;
	}

	/* init slot */
	rv = mToken->Login(PR_TRUE);
	if (NS_FAILED(rv)) return rv;
#endif
  
	do {
		rv = import_from_file_helper (pkcs12, path, &wantRetry, error);
	} while (rv && wantRetry);

	return rv;
}

gboolean
e_pkcs12_export_to_file (EPKCS12 *pkcs12, const char *path, GList *certs, GError **error)
{
}

/* what to do when the nickname collides with one already in the db.
   TODO: not handled, throw a dialog allowing the nick to be changed? */
static SECItem * PR_CALLBACK
nickname_collision(SECItem *oldNick, PRBool *cancel, void *wincx)
{
	/* nsNSSShutDownPreventionLock locker; */
	int count = 1;
	char *nickname = NULL;
	char *default_nickname = _("Imported Certificate");
	SECItem *new_nick;

	*cancel = PR_FALSE;
	printf ("nickname_collision\n");

	/* The user is trying to import a PKCS#12 file that doesn't have the
	   attribute we use to set the nickname.  So in order to reduce the
	   number of interactions we require with the user, we'll build a nickname
	   for the user.  The nickname isn't prominently displayed in the UI, 
	   so it's OK if we generate one on our own here.
	   XXX If the NSS API were smarter and actually passed a pointer to
	       the CERTCertificate* we're importing we could actually just
	       call default_nickname (which is what the issuance code path
	       does) and come up with a reasonable nickname.  Alas, the NSS
	       API limits our ability to produce a useful nickname without
	       bugging the user.  :(
	*/
	while (1) {
		CERTCertificate *cert;

		/* If we've gotten this far, that means there isn't a certificate
		   in the database that has the same subject name as the cert we're
		   trying to import.  So we need to come up with a "nickname" to 
		   satisfy the NSS requirement or fail in trying to import.  
		   Basically we use a default nickname from a properties file and 
		   see if a certificate exists with that nickname.  If there isn't, then
		   create update the count by one and append the string '#1' Or 
		   whatever the count currently is, and look for a cert with 
		   that nickname.  Keep updating the count until we find a nickname
		   without a corresponding cert.
		   XXX If a user imports *many* certs without the 'friendly name'
		   attribute, then this may take a long time.  :(
		*/
		if (count > 1) {
			g_free (nickname);
			nickname = g_strdup_printf ("%s #%d", default_nickname, count);
		} else {
			g_free (nickname);
			nickname = g_strdup (default_nickname);
		}
		cert = CERT_FindCertByNickname(CERT_GetDefaultCertDB(),
					       nickname);
		if (!cert) {
			break;
		}
		CERT_DestroyCertificate(cert);
		count++;
	}

	new_nick = PR_Malloc (sizeof (SECItem));
	new_nick->type = siAsciiString;
	new_nick->data = nickname;
	new_nick->len  = strlen((char*)new_nick->data);
	return new_nick;
}

/* write bytes to the exported PKCS#12 file */
static void PR_CALLBACK
write_export_file(void *arg, const char *buf, unsigned long len)
{
	EPKCS12 *pkcs12 = E_PKCS12 (arg);
	EPKCS12Private *priv = pkcs12->priv;

	printf ("write_export_file\n");

	write (priv->tmp_fd, buf, len);
}

static gboolean
handle_error(int myerr)
{
	printf ("handle_error (%d)\n", myerr);
}
