/*
   +----------------------------------------------------------------------+
   | Server API Abstraction Layer                                         |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998, 1999 SAPI Development Team                       |
   +----------------------------------------------------------------------+
   | This source file is subject to the Zend license, that is bundled     |
   | with this package in the file LICENSE.  If you did not receive a     |
   | copy of the Zend license, please mail us at zend@zend.com so we can  |
   | send you a copy immediately.                                         |
   +----------------------------------------------------------------------+
   | Design:  Shane Caraveo <shane@caraveo.com>                           |
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   +----------------------------------------------------------------------+
*/


#include "SAPI.h"
#ifdef ZTS
#include "TSRM.h"
#endif

#if WIN32||WINNT
#define STRCASECMP stricmp
#else
#define STRCASECMP strcasecmp
#endif


SAPI_API void (*sapi_error)(int error_type, const char *message, ...);


#ifdef ZTS
SAPI_API int sapi_globals_id;
#else
sapi_globals_struct sapi_globals;
#endif


/* True globals (no need for thread safety) */
sapi_module_struct sapi_module;
SAPI_API void (*sapi_error)(int error_type, const char *message, ...);


SAPI_API void sapi_startup(sapi_module_struct *sf)
{
	sapi_module = *sf;
#ifdef ZTS
	sapi_globals_id = ts_allocate_id(sizeof(sapi_globals_struct), NULL, NULL);
#endif
}


static void sapi_free_header(sapi_header_struct *sapi_header)
{
	efree(sapi_header->header);
}


SAPI_API void sapi_activate(SLS_D)
{
	zend_llist_init(&SG(sapi_headers).headers, sizeof(sapi_header_struct), (void (*)(void *)) sapi_free_header, 0);
	SG(sapi_headers).content_type.header = NULL;
	SG(sapi_headers).http_response_code = 200;
	SG(headers_sent) = 0;
}


SAPI_API void sapi_deactivate(SLS_D)
{
	zend_llist_destroy(&SG(sapi_headers).headers);
	if (SG(sapi_headers).content_type.header) {
		efree(SG(sapi_headers).content_type.header);
	}
}


/* This function expects a *duplicated* string, that was previously emalloc()'d.
 * Pointers sent to this functions will be automatically freed by the framework.
 */
SAPI_API int sapi_add_header(char *header_line, uint header_line_len)
{
	int retval;
	sapi_header_struct sapi_header;
	SLS_FETCH();

	if (SG(headers_sent)) {
		sapi_module.sapi_error(E_WARNING, "Cannot add header information - headers already sent");
		efree(header_line);
		return FAILURE;
	}

	sapi_header.header = header_line;
	sapi_header.header_len = header_line_len;

	if (sapi_module.header_handler) {
		retval = sapi_module.header_handler(&sapi_header, &SG(sapi_headers));
	} else {
		retval = SAPI_HEADER_ADD;
	}

	if (retval & SAPI_HEADER_DELETE_ALL) {
		zend_llist_clean(&SG(sapi_headers).headers);
	}
	if (retval & SAPI_HEADER_ADD) {
		char *colon_offset = strchr(header_line, ':');

		if (colon_offset) {
			*colon_offset = 0;
			if (!STRCASECMP(header_line, "Content-Type")) {
				if (SG(sapi_headers).content_type.header) {
					efree(SG(sapi_headers).content_type.header);
				}
				*colon_offset = ':';
				SG(sapi_headers).content_type.header = (char *) header_line;
				SG(sapi_headers).content_type.header_len = header_line_len;
				return SUCCESS;
			}
			*colon_offset = ':';
		}
		zend_llist_add_element(&SG(sapi_headers).headers, (void *) &sapi_header);
	}
	return SUCCESS;
}


SAPI_API int sapi_send_headers()
{
	int retval;
	sapi_header_struct default_header = { SAPI_DEFAULT_CONTENT_TYPE, sizeof(SAPI_DEFAULT_CONTENT_TYPE)-1 };
	SLS_FETCH();

	if (SG(headers_sent)) {
		return SUCCESS;
	}

	if (sapi_module.send_headers) {
		retval = sapi_module.send_headers(&SG(sapi_headers) SLS_CC);
	} else {
		retval = SAPI_HEADER_DO_SEND;
	}

	switch (retval) {
		case SAPI_HEADER_SENT_SUCCESSFULLY:
			SG(headers_sent) = 1;
			return SUCCESS;
			break;
		case SAPI_HEADER_DO_SEND:
			if (SG(sapi_headers).content_type.header) {
				sapi_module.send_header(&SG(sapi_headers).content_type, SG(server_context));
			} else {
				sapi_module.send_header(&default_header, SG(server_context));
			}
			zend_llist_apply_with_argument(&SG(sapi_headers).headers, (void (*)(void *, void *)) sapi_module.send_header, SG(server_context));
			sapi_module.send_header(NULL, SG(server_context));
			SG(headers_sent) = 1;
			return SUCCESS;
			break;
		case SAPI_HEADER_SEND_FAILED:
			return FAILURE;
			break;
	}
	return FAILURE;
}
