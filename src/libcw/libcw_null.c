/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


/**
   \file libcw_null.c

   \brief Null audio sink.

   No sound is being played, but time periods necessary for generator
   to operate are being measured.
*/





#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
#include <stdbool.h>


#include "libcw_null.h"
#include "libcw_utils.h"
#include "libcw_gen.h"





static int  cw_null_open_device_internal(cw_gen_t *gen);
static void cw_null_close_device_internal(cw_gen_t *gen);



int cw_null_configure(cw_gen_t *gen, const char *device)
{
	assert (gen);

	gen->audio_system = CW_AUDIO_NULL;
	cw_gen_set_audio_device_internal(gen, device);

	gen->open_device  = cw_null_open_device_internal;
	gen->close_device = cw_null_close_device_internal;
	// gen->write        = cw_null_write; /* The function is called in libcw.c explicitly/directly, not by a pointer. */

	gen->sample_rate = 48000; /* Some asserts may check for non-zero
				     value of sample rate or its derivatives. */

	return CW_SUCCESS;
}






bool cw_is_null_possible(__attribute__((unused)) const char *device)
{
	return true;
}





int cw_null_open_device_internal(cw_gen_t *gen)
{
	gen->audio_device_is_open = true;
	return CW_SUCCESS;
}





void cw_null_close_device_internal(cw_gen_t *gen)
{
	gen->audio_device_is_open = false;
	return;
}





void cw_null_write(__attribute__((unused)) cw_gen_t *gen, cw_tone_t *tone)
{
	assert (gen);
	assert (gen->audio_system == CW_AUDIO_NULL);
	assert (tone->len >= 0); /* TODO: shouldn't the condition be "tone->len > 0"? */

	struct timespec n = { .tv_sec = 0, .tv_nsec = 0 };
	cw_usecs_to_timespec_internal(&n, tone->len);

	cw_nanosleep_internal(&n);

	return;
}
