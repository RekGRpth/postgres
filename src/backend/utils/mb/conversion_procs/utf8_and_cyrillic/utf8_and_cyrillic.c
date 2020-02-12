/*-------------------------------------------------------------------------
 *
 *	  UTF8 and Cyrillic
 *
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/mb/conversion_procs/utf8_and_cyrillic/utf8_and_cyrillic.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "../../Unicode/utf8_to_koi8r.map"
#include "../../Unicode/koi8r_to_utf8.map"
#include "../../Unicode/utf8_to_koi8u.map"
#include "../../Unicode/koi8u_to_utf8.map"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(utf8_to_koi8r);
PG_FUNCTION_INFO_V1(koi8r_to_utf8);

PG_FUNCTION_INFO_V1(utf8_to_koi8u);
PG_FUNCTION_INFO_V1(koi8u_to_utf8);

/* ----------
 * conv_proc(
 *		INTEGER,	-- source encoding id
 *		INTEGER,	-- destination encoding id
 *		CSTRING,	-- source string (null terminated C string)
 *		CSTRING,	-- destination string (null terminated C string)
 *		INTEGER		-- source string length
 * ) returns VOID;
 * ----------
 */

static uint32
conv_utf8_to_koi8(uint32 code)
{
	ereport(WARNING, (errcode(ERRCODE_UNTRANSLATABLE_CHARACTER), errmsg("character with byte sequence 0x%x in encoding \"UTF8\" has no equivalent in encoding \"KOI8\"", code)));
	switch (code) {
		case 0xE28496: return 0x23;
		case 0xC2AB: return 0x22;
		case 0xC2BB: return 0x22;
		default: return 0x20;
	}
}

Datum
utf8_to_koi8r(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);

	CHECK_ENCODING_CONVERSION_ARGS(PG_UTF8, PG_KOI8R);

	UtfToLocal(src, len, dest,
			   &koi8r_from_unicode_tree,
			   NULL, 0,
			   conv_utf8_to_koi8,
			   PG_KOI8R);

	PG_RETURN_VOID();
}

Datum
koi8r_to_utf8(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);

	CHECK_ENCODING_CONVERSION_ARGS(PG_KOI8R, PG_UTF8);

	LocalToUtf(src, len, dest,
			   &koi8r_to_unicode_tree,
			   NULL, 0,
			   NULL,
			   PG_KOI8R);

	PG_RETURN_VOID();
}

Datum
utf8_to_koi8u(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);

	CHECK_ENCODING_CONVERSION_ARGS(PG_UTF8, PG_KOI8U);

	UtfToLocal(src, len, dest,
			   &koi8u_from_unicode_tree,
			   NULL, 0,
			   conv_utf8_to_koi8,
			   PG_KOI8U);

	PG_RETURN_VOID();
}

Datum
koi8u_to_utf8(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);

	CHECK_ENCODING_CONVERSION_ARGS(PG_KOI8U, PG_UTF8);

	LocalToUtf(src, len, dest,
			   &koi8u_to_unicode_tree,
			   NULL, 0,
			   NULL,
			   PG_KOI8U);

	PG_RETURN_VOID();
}
