/*				       	-*- c-file-style: "bsd" -*-
 * rproxy -- dynamic caching and delta update in HTTP
 * $Id$
 * 
 * Copyright (C) 2000 by Martin Pool <mbp@humbug.org.au>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
     XXX
      XXX
       XXX
        XXX    THIS FILE IS GOING AWAY SOON!
         XXX   filebufs are deprecated in favour of mapptr, etc
          XXX
           XXX
            XXX
  */

#include "includes.h"
#include "protocol.h"
#include "command.h"
#include "inhale.h"

/* For debugging porpoises, here are some human-readable forms. */
struct hs_op_kind_name const _hs_op_kind_names[] = {
    {"EOF", op_kind_eof},
    {"COPY", op_kind_copy},
    {"LITERAL", op_kind_literal},
    {"SIGNATURE", op_kind_signature}
};


static int
_hs_is_gd_eof(uint8_t cmd)
{
    return cmd == 0;
}


static int
_hs_is_gd_copy(uint8_t type, uint32_t * offset, uint32_t * length,
	       hs_read_fn_t read_fn, void *read_priv)
{
    uint8_t         tmp8;
    uint16_t        tmp16;
    uint32_t        tmp32;

    if (type < op_copy_short_byte || type > op_copy_int_int)
	return 0;		/* nope */

    /* read the first parameter, being the offset */
    if (type == op_copy_short_byte
	|| type == op_copy_short_short || type == op_copy_short_int) {
	if (_hs_read_netshort(read_fn, read_priv, &tmp16) != sizeof tmp16)
	    return -1;
	*offset = tmp16;
    } else {
	/* must be an int */
	if (_hs_read_netint(read_fn, read_priv, &tmp32) != sizeof tmp32)
	    return -1;
	*offset = tmp32;
    }

    /* read the second, being the length. */
    if (type == op_copy_short_byte || type == op_copy_int_byte) {
	if (_hs_read_netbyte(read_fn, read_priv, &tmp8) != sizeof tmp8)
	    return -1;
	*length = tmp8;
    } else if (type == op_copy_short_short || type == op_copy_int_short) {
	if (_hs_read_netshort(read_fn, read_priv, &tmp16) != sizeof tmp16)
	    return -1;
	*length = tmp16;
    } else {
	if (_hs_read_netint(read_fn, read_priv, &tmp32) != sizeof tmp32)
	    return -1;
	*length = tmp32;
    }

    return 1;
}


static int
_hs_is_gd_literal(uint8_t cmd,
		  uint32_t * length, hs_read_fn_t read_fn, void *read_priv)
{
    int             ret = 0;

    if (cmd == op_literal_int) {
	ret = _hs_read_netint(read_fn, read_priv, length);
    } else if (cmd == op_literal_short) {
	uint16_t        tmp;

	ret = _hs_read_netshort(read_fn, read_priv, &tmp);
	*length = tmp;
    } else if (cmd == op_literal_byte) {
	uint8_t         tmp;

	ret = _hs_read_netbyte(read_fn, read_priv, &tmp);
	*length = tmp;
    } else if (cmd >= op_literal_1 && cmd < op_literal_byte) {
	*length = cmd - op_literal_1 + 1;
	ret = 1;
    }

    return ret;
}



static int
_hs_is_gd_signature(uint8_t cmd,
		    uint32_t * length, hs_read_fn_t read_fn, void *read_priv)
{
    int             ret = 0;

    if (cmd == op_signature_int) {
	ret = _hs_read_netint(read_fn, read_priv, length);
    } else if (cmd == op_signature_short) {
	uint16_t        tmp;

	ret = _hs_read_netshort(read_fn, read_priv, &tmp);
	*length = tmp;
    } else if (cmd == op_signature_byte) {
	uint8_t         tmp;

	ret = _hs_read_netbyte(read_fn, read_priv, &tmp);
	*length = tmp;
    } else if (cmd >= op_signature_1 && cmd < op_signature_byte) {
	*length = cmd - op_signature_1 + 1;
	ret = 1;
    }

    return ret;
}


static int
_hs_is_op_checksum(uint8_t cmd,
		   uint32_t * length, hs_read_fn_t read_fn, void *read_priv)
{
    int             ret = 0;

    if (cmd == op_checksum_short) {
	uint16_t        tmp;

	ret = _hs_read_netshort(read_fn, read_priv, &tmp);
	*length = tmp;
    }
    return ret;
}


/* 
   Returns: -1 on error, 0 on hard eof, or +1 for a command.  The command is
   in KIND, and LEN and OFF are set if appropriate. */
int
_hs_inhale_command(hs_read_fn_t read_fn, void *read_priv,
		   int *kind, uint32_t * len, uint32_t * off)
{
    int             ret;
    uint8_t         type;

    ret = _hs_read_loop(read_fn, read_priv, &type, 1);
    if (ret > 1) {
	_hs_error("long read while trying to get a one-byte command!");
	return -1;
    } else if (ret < 0) {
	_hs_error("error while trying to read command byte");
	return -1;
    } else if (ret == 0) {
	_hs_error("unexpected end of file while reading a command byte; "
		  "assuming that this was meant to be the end of the file");
	*kind = op_kind_eof;
	return 0;
    }

    if (_hs_is_gd_eof(type) > 0) {
	*kind = op_kind_eof;
    } else if (_hs_is_gd_literal(type, len, read_fn, read_priv) > 0) {
	*kind = op_kind_literal;
    } else if (_hs_is_gd_signature(type, len, read_fn, read_priv) > 0) {
	*kind = op_kind_signature;
    } else if (_hs_is_gd_copy(type, off, len, read_fn, read_priv) > 0) {
	*kind = op_kind_copy;
    } else if (_hs_is_op_checksum(type, len, read_fn, read_priv) > 0) {
	*kind = op_kind_checksum;
    } else {
	_hs_fatal("unexpected command %#x!", type);
	return -1;
    }

    return 1;
}