/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdio.h>

#include "camel-mime-message.h"
#include "camel-mime-body-part.h"
#include "camel-multipart.h"
#include "camel-stream.h"
#include "camel-stream-fs.h"
#include "camel-stream-data-wrapper.h"
#include "camel.h"

int
main (int argc, char**argv)
{
	CamelMimeMessage *message;
	CamelMultipart *multipart;
	CamelMimeBodyPart *body_part;
	CamelMimeBodyPart *attachment_part;
	CamelStream *attachment_stream;
	
	/*  FILE *output_file; */
	CamelStream *stream;

	gtk_init (&argc, &argv);
	camel_init ();

	if (argc < 2) {
		attachment_stream = NULL;
	} else {
		if (argc == 2) {
			attachment_stream = camel_stream_fs_new_with_name
				(argv[1], CAMEL_STREAM_FS_READ);
			if (attachment_stream == NULL) {
				fprintf (stderr, "Cannot open `%s'\n",
					 argv[1]);
				return 1;
			}
		} else {
			fprintf (stderr, "Usage: %s [<attachment>]\n",
				 argv[0]);
			return 1;
		}
	}

	message = camel_mime_message_new ();

	camel_mime_part_set_description (CAMEL_MIME_PART (message), "a test");

	camel_medium_add_header (CAMEL_MEDIUM (message), "X-test1", "the value of a test");
	camel_medium_add_header (CAMEL_MEDIUM (message), "X-test2", "the value of another test");
	/*camel_mime_part_add_content_language (CAMEL_MIME_PART (message), g_string_new ("es-ca"));*/

	camel_mime_message_set_date (message, CAMEL_MESSAGE_DATE_CURRENT, 0);
	camel_mime_message_set_subject (message, g_strdup ("A test message"));
	camel_mime_message_set_reply_to (message, g_strdup ("toto@toto.com"));
	camel_mime_message_set_from (message, g_strdup ("Bertrand.Guiheneuf@aful.org"));

	camel_mime_message_add_recipient (message, CAMEL_RECIPIENT_TYPE_TO, 
					  g_strdup ("franck.dechamps@alseve.fr"));
	camel_mime_message_add_recipient (message, CAMEL_RECIPIENT_TYPE_TO, 
					  g_strdup ("mc@alseve.fr"));
	camel_mime_message_add_recipient (message, CAMEL_RECIPIENT_TYPE_TO, 
					  g_strdup ("richard.lengagne@inria.fr"));
	camel_mime_message_add_recipient (message, CAMEL_RECIPIENT_TYPE_CC, 
					  g_strdup ("Francois.fleuret@inria.fr"));
	camel_mime_message_add_recipient (message, CAMEL_RECIPIENT_TYPE_CC, 
					  g_strdup ("maury@justmagic.com"));
 	camel_mime_message_add_recipient (message, CAMEL_RECIPIENT_TYPE_BCC, 
					  g_strdup ("Bertrand.Guiheneuf@aful.org"));

	multipart = camel_multipart_new ();
	body_part = camel_mime_body_part_new ();
	camel_mime_part_set_text (CAMEL_MIME_PART (body_part), "This is a test.\nThis is only a test.\n");
	camel_multipart_add_part (multipart, body_part);

	if (attachment_stream == NULL) {
		attachment_part = NULL;
	} else {
		CamelDataWrapper *attachment_wrapper;
		
		/*CamelDataWrapper *stream_wrapper;

		stream_wrapper = camel_stream_data_wrapper_new
			                                   (attachment_stream);

		attachment_part = camel_mime_body_part_new ();
		camel_mime_part_set_encoding (CAMEL_MIME_PART (attachment_part),
					      CAMEL_MIME_PART_ENCODING_BASE64);
		camel_medium_set_content_object (CAMEL_MEDIUM (attachment_part),
						 stream_wrapper);
		camel_multipart_add_part (multipart, attachment_part);

		gtk_object_unref (GTK_OBJECT (stream_wrapper));*/
		
		attachment_wrapper = CAMEL_DATA_WRAPPER (camel_simple_data_wrapper_new ());
		camel_data_wrapper_set_input_stream (attachment_wrapper, 
						     attachment_stream);
		
		attachment_part = camel_mime_body_part_new ();
		camel_mime_part_set_encoding (CAMEL_MIME_PART (attachment_part),
					      CAMEL_MIME_PART_ENCODING_BASE64);
		camel_medium_set_content_object (CAMEL_MEDIUM (attachment_part),
						 attachment_wrapper);
		camel_multipart_add_part (multipart, attachment_part);

		

	}
	
	camel_medium_set_content_object (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (multipart));

	stream = camel_stream_fs_new_with_name ("mail1.test", CAMEL_STREAM_FS_WRITE );
	if (!stream)  {
		printf ("could not open output file");
		exit(2);
	}
		       
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message),
					    stream);
	camel_stream_close (stream);
	gtk_object_unref (GTK_OBJECT (stream));

	gtk_object_unref (GTK_OBJECT (message));
	gtk_object_unref (GTK_OBJECT (multipart));
	gtk_object_unref (GTK_OBJECT (body_part));

	if (attachment_part != NULL)
		gtk_object_unref (GTK_OBJECT (attachment_part));
	
	printf ("Test1 finished\n");
	return 1;
}
 
