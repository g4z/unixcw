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
   \file libcw_console.c

   \brief Console buzzer audio sink.
*/





#include "config.h"


#ifdef LIBCW_WITH_CONSOLE



#include <sys/time.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>


#if   defined(HAVE_SYS_KD_H)
#       include <sys/kd.h>
#elif defined(HAVE_SYS_VTKD_H)
#       include <sys/vtkd.h>
#elif defined(HAVE_SYS_KBIO_H)
#       include <sys/kbio.h>
#endif

#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#ifdef __FreeBSD__
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#endif


#include "libcw_console.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_gen.h"


extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;



/* Clock tick rate used for KIOCSOUND console ioctls.  This value is taken
   from linux/include/asm-i386/timex.h, included here for portability. */
static const int KIOCSOUND_CLOCK_TICK_RATE = 1193180;

static void cw_console_close_device_internal(cw_gen_t *gen);
static int  cw_console_open_device_internal(cw_gen_t *gen);
static int  cw_console_write_low_level_internal(cw_gen_t *gen, bool state);





/**
   \brief Check if it is possible to open console output

   Function does a test opening and test writing to console device,
   but it closes it before returning.

   The function tests that the given console file exists, and that it
   will accept the KIOCSOUND ioctl.  It unconditionally returns false
   on platforms that do no support the KIOCSOUND ioctl.

   Call to ioctl will fail if calling code doesn't have root privileges.

   This is the only place where we ask if KIOCSOUND is defined, so client
   code must call this function whenever it wants to use console output,
   as every other function called to perform console operations will
   happily assume that it is allowed to perform such operations.

   \param device - name of console device to be used; if NULL then library will use default device.

   \return true if opening console output succeeded;
   \return false if opening console output failed;
*/
bool cw_is_console_possible(const char *device)
{
	/* no need to allocate space for device path, just a
	   pointer (to a memory allocated somewhere else by
	   someone else) will be sufficient in local scope */
	const char *dev = device ? device : CW_DEFAULT_CONSOLE_DEVICE;

	int fd = open(dev, O_WRONLY);
	if (fd == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_console: open(%s): %s", dev, strerror(errno));
		return false;
	}

	int rv = ioctl(fd, KIOCSOUND, 0);
	close(fd);
	if (rv == -1) {
		/* console device can be opened, even with WRONLY perms, but,
		   if you aren't root user, you can't call ioctl()s on it,
		   and - as a result - can't generate sound on the device */
		return false;
	} else {
		return true;
	}
}





/**
   \brief Open console PC speaker device associated with given generator

   The function doesn't check if ioctl(fd, KIOCSOUND, ...) works,
   the client code must use cw_is_console_possible() instead, prior
   to calling this function.

   You must use cw_gen_set_audio_device_internal() before calling
   this function. Otherwise generator \p gen won't know which device to open.

   \param gen - current generator

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_console_open_device_internal(cw_gen_t *gen)
{
	assert (gen->audio_device);

	if (gen->audio_device_is_open) {
		/* Ignore the call if the console device is already open. */
		return CW_SUCCESS;
	}

	int console = open(gen->audio_device, O_WRONLY);
	if (console == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_console: open(%s): \"%s\"", gen->audio_device, strerror(errno));
		return CW_FAILURE;
        } else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      "libcw_console: open successfully, console = %d", console);
	}

	/* It doesn't have any sense for console, but some code may depend
	   on non-zero value of sample rate. */
	gen->sample_rate = 44100;

	gen->audio_sink = console;
	gen->audio_device_is_open = true;

	return CW_SUCCESS;
}





void cw_console_silence(cw_gen_t *gen)
{
	ioctl(gen->audio_sink, KIOCSOUND, 0);
	return;
}





/**
   \brief Close console device associated with current generator
*/
void cw_console_close_device_internal(cw_gen_t *gen)
{
	close(gen->audio_sink);
	gen->audio_sink = -1;
	gen->audio_device_is_open = false;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      "libcw_console: console closed");

	return;
}





/**
   \brief Pseudo-device for playing sound with console

   Function behaving like a device, to which one does a blocking write.
   It generates sound with parameters (frequency and duration) specified
   in \p tone..
   After playing X microseconds of tone it returns. It is intended
   to behave like a blocking write() function.

   \param gen - current generator
   \param tone - tone to play with generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_console_write(cw_gen_t *gen, cw_tone_t *tone)
{
	assert (gen);
	assert (gen->audio_system == CW_AUDIO_CONSOLE);
	assert (tone->len >= 0); /* TODO: shouldn't the condition be "tone->len > 0"? */

	struct timespec n = { .tv_sec = 0, .tv_nsec = 0 };
	cw_usecs_to_timespec_internal(&n, tone->len);

	int rv = cw_console_write_low_level_internal(gen, (bool) tone->frequency);
	cw_nanosleep_internal(&n);

	if (tone->slope_mode == CW_SLOPE_MODE_FALLING_SLOPE) {
		/* Falling slope causes the console to produce sound, so at
		   the end of the slope - the console is left in "generate"
		   state. We have to explicitly stop generating sound at
		   the end of falling slope. */
		rv &= cw_console_write_low_level_internal(gen, false);
	} else if (tone->slope_mode == CW_SLOPE_MODE_STANDARD_SLOPES) {
		/* It seems that it's a good idea to turn off the buzzer
		   after playing standard tone. In theory the buzzer would be
		   turned off by "silence" tone coming right after an after
		   audible tone, but in practice it may not be always so.*/
		rv &= cw_console_write_low_level_internal(gen, false);
	} else {
		;
	}

	return rv;
}





/**
   \brief Start generating a sound using console PC speaker

   The function calls the KIOCSOUND ioctl to start a particular tone.
   Once started, the console tone generation needs no maintenance.

   The function only initializes generation, you have to do another
   function call to change the tone generated.

   \param gen - current generator
   \param state - flag deciding if a sound should be generated (> 0) or not (== 0)

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_console_write_low_level_internal(cw_gen_t *gen, bool state)
{
	static bool local_state = false;
	if (local_state == state) {
		return CW_SUCCESS;
	} else {
		local_state = state;
	}

	/* TODO: take a look at KDMKTONE ioctl argument. */

	/* Calculate the correct argument for KIOCSOUND.  There's nothing we
	   can do to control the volume, but if we find the volume is set to
	   zero, the one thing we can do is to just turn off tones.  A bit
	   crude, but perhaps just slightly better than doing nothing. */
	int argument = 0;
	if (gen->volume_percent > 0 && local_state) {
		argument = KIOCSOUND_CLOCK_TICK_RATE / gen->frequency;
	}

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      "libcw_console: KIOCSOUND arg = %d (switch: %d, frequency: %d Hz, volume: %d %%)",
		      argument, local_state, gen->frequency, gen->volume_percent);

	if (ioctl(gen->audio_sink, KIOCSOUND, argument) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_console: ioctl KIOCSOUND: \"%s\"", strerror(errno));
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





int cw_console_configure(cw_gen_t *gen, const char *device)
{
	assert (gen);

	gen->audio_system = CW_AUDIO_CONSOLE;
	cw_gen_set_audio_device_internal(gen, device);

	gen->open_device  = cw_console_open_device_internal;
	gen->close_device = cw_console_close_device_internal;
	// gen->write        = cw_console_write; // The function is called in libcw.c directly/explicitly, not through a pointer. */

	return CW_SUCCESS;
}





#else /* #ifdef LIBCW_WITH_CONSOLE */





#include "libcw_console.h"





bool cw_is_console_possible(__attribute__((unused)) const char *device)
{
	return false;
}





int cw_console_configure(__attribute__((unused)) cw_gen_t *gen, __attribute__((unused)) const char *device)
{
	return CW_FAILURE;
}





int cw_console_write(__attribute__((unused)) cw_gen_t *gen, __attribute__((unused)) cw_tone_t *tone)
{
	return CW_FAILURE;
}




void cw_console_silence(__attribute__((unused)) cw_gen_t *gen)
{
	return;
}





#endif /* #ifdef LIBCW_WITH_CONSOLE */
