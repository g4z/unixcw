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
   \file libcw_oss.c

   \brief OSS audio sink.
*/





#include "config.h"


#ifdef LIBCW_WITH_OSS




#include <stdio.h>
#include <string.h>

#include <dlfcn.h> /* dlopen() and related symbols */

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>


#include "libcw_oss.h"
#include "libcw_debug.h"
#include "libcw_gen.h"


#if   defined(HAVE_SYS_SOUNDCARD_H)
#       include <sys/soundcard.h>
#elif defined(HAVE_SOUNDCARD_H)
#       include <soundcard.h>
#else
#
#endif


extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;


extern const unsigned int cw_supported_sample_rates[];
extern const char *default_audio_devices[];



/* Conditional compilation flags */
#define CW_OSS_SET_FRAGMENT       1  /* ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &param) */
#define CW_OSS_SET_POLICY         0  /* ioctl(fd, SNDCTL_DSP_POLICY, &param) */

/* Constants specific to OSS audio system configuration */
static const int CW_OSS_SETFRAGMENT = 7;              /* Sound fragment size, 2^7 samples */
static const int CW_OSS_SAMPLE_FORMAT = AFMT_S16_NE;  /* Sound format AFMT_S16_NE = signed 16 bit, native endianess; LE = Little endianess */

static int  cw_oss_open_device_ioctls_internal(int *fd, int *sample_rate);
static int  cw_oss_get_version_internal(int fd, int *x, int *y, int *z);
static int  cw_oss_write_internal(cw_gen_t *gen);
static int  cw_oss_open_device_internal(cw_gen_t *gen);
static void cw_oss_close_device_internal(cw_gen_t *gen);





/**
   \brief Check if it is possible to open OSS output

   Function does a test opening and test configuration of OSS output,
   but it closes it before returning.

   \param device - name of OSS device to be used; if NULL then library will use default device.

   \return true if opening OSS output succeeded;
   \return false if opening OSS output failed;
*/
bool cw_is_oss_possible(const char *device)
{
	const char *dev = device ? device : CW_DEFAULT_OSS_DEVICE;
	/* Open the given soundcard device file, for write only. */
	int soundcard = open(dev, O_WRONLY);
	if (soundcard == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: open(%s): \"%s\"", dev, strerror(errno));
		return false;
        }

	{
		int x = 0, y = 0, z = 0;
		int rv = cw_oss_get_version_internal(soundcard, &x, &y, &z);
		if (rv == CW_FAILURE) {
			close(soundcard);
			return false;
		} else {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
				      "cw_oss: OSS version %X.%X.%X", x, y, z);
		}
	}

	/*
	  http://manuals.opensound.com/developer/OSS_GETVERSION.html:
	  about OSS_GETVERSION ioctl:
	  "This ioctl call returns the version number OSS API used in
	  the current system. Applications can use this information to
	  find out if the OSS version is new enough to support the
	  features required by the application. However this methods
	  should be used with great care. Usually it's recommended
	  that applications check availability of each ioctl() by
	  calling it and by checking if the call returned errno=EINVAL."

	  So, we call all necessary ioctls to be 100% sure that all
	  needed features are available. cw_oss_open_device_ioctls_internal()
	  doesn't specifically look for EINVAL, it only checks return
	  values from ioctl() and returns CW_FAILURE if one of ioctls()
	  returns -1. */
	int dummy;
	int rv = cw_oss_open_device_ioctls_internal(&soundcard, &dummy);
	close(soundcard);
	if (rv != CW_SUCCESS) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: one or more OSS ioctl() calls failed");
		return false;
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      "cw_oss: OSS is possible");
		return true;
	}
}





int cw_oss_configure(cw_gen_t *gen, const char *device)
{
	assert (gen);

	gen->audio_system = CW_AUDIO_OSS;
	cw_gen_set_audio_device_internal(gen, device);

	gen->open_device  = cw_oss_open_device_internal;
	gen->close_device = cw_oss_close_device_internal;
	gen->write        = cw_oss_write_internal;

	return CW_SUCCESS;
}





int cw_oss_write_internal(cw_gen_t *gen)
{
	assert (gen);
	assert (gen->audio_system == CW_AUDIO_OSS);

	int n_bytes = sizeof (gen->buffer[0]) * gen->buffer_n_samples;
	int rv = write(gen->audio_sink, gen->buffer, n_bytes);
	if (rv != n_bytes) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: audio write: %s", strerror(errno));
		//return NULL;
	}
	// cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO, "cw_oss: written %d samples", gen->buffer_n_samples);


	return CW_SUCCESS;
}





/**
   \brief Open OSS output, associate it with given generator

   You must use cw_gen_set_audio_device_internal() before calling
   this function. Otherwise generator \p gen won't know which device to open.

   \param gen - current generator

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_oss_open_device_internal(cw_gen_t *gen)
{
	/* Open the given soundcard device file, for write only. */
	int soundcard = open(gen->audio_device, O_WRONLY);
	if (soundcard == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: open(%s): \"%s\"", gen->audio_device, strerror(errno));
		return CW_FAILURE;
        }

	int rv = cw_oss_open_device_ioctls_internal(&soundcard, &gen->sample_rate);
	if (rv != CW_SUCCESS) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: one or more OSS ioctl() calls failed");
		close(soundcard);
		return CW_FAILURE;
	}


	int size = 0;
	/* Get fragment size in bytes, may be different than requested
	   with ioctl(..., SNDCTL_DSP_SETFRAGMENT), and, in particular,
	   can be different than 2^N. */
	if ((rv = ioctl(soundcard, (int) SNDCTL_DSP_GETBLKSIZE, &size)) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_GETBLKSIZE): \"%s\"", strerror(errno));
		close(soundcard);
		return CW_FAILURE;
        }

	if ((size & 0x0000ffff) != (1 << CW_OSS_SETFRAGMENT)) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: OSS fragment size not set, %d", size);
		close(soundcard);
		/* FIXME */
		return CW_FAILURE;
        } else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
			      "cw_oss: OSS fragment size = %d", size);
	}
	gen->buffer_n_samples = size;


	cw_oss_get_version_internal(soundcard, &gen->oss_version.x, &gen->oss_version.y, &gen->oss_version.z);

	/* Note sound as now open for business. */
	gen->audio_device_is_open = true;
	gen->audio_sink = soundcard;

#if CW_DEV_RAW_SINK
	gen->dev_raw_sink = open("/tmp/cw_file.oss.raw", O_WRONLY | O_TRUNC | O_NONBLOCK);
#endif

	return CW_SUCCESS;
}





/**
   \brief Perform all necessary ioctl calls on OSS file descriptor

   Wrapper function for ioctl calls that need to be done when configuring
   file descriptor \param fd for OSS playback.

   \param fd - file descriptor of open OSS file;
   \param sample_rate - sample rate configured by ioctl calls (output parameter)

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_oss_open_device_ioctls_internal(int *fd, int *sample_rate)
{
	int parameter = 0; /* ignored */
	if (ioctl(*fd, SNDCTL_DSP_SYNC, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_SYNC): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }
#if 0
	/*
	   This ioctl call failed on FreeBSD 10, which resulted in
	   libcw failing to open OSS device. A bit of digging on the
	   web revealed this:

	   OSS4:
	   http://manuals.opensound.com/developer/SNDCTL_DSP_POST.html:
	   "This ioctl call is provided for compatibility with older
	   applications. It has no practical purpose and should in no
	   case be used in new applications."
	*/

	parameter = 0; /* ignored */
	if (ioctl(*fd, SNDCTL_DSP_POST, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_POST): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }
#endif
	/* Set the audio format to 8-bit unsigned. */
	parameter = CW_OSS_SAMPLE_FORMAT;
	if (ioctl(*fd, (int) SNDCTL_DSP_SETFMT, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_SETFMT): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }
	if (parameter != CW_OSS_SAMPLE_FORMAT) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: sample format not supported");
		return CW_FAILURE;
        }

	/* Set up mono mode - a single audio channel. */
	parameter = CW_AUDIO_CHANNELS;
	if (ioctl(*fd, (int) SNDCTL_DSP_CHANNELS, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_CHANNELS): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }
	if (parameter != CW_AUDIO_CHANNELS) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: number of channels not supported");
		return CW_FAILURE;
        }

	/* Set up a standard sampling rate based on the notional correct
	   value, and retain the one we actually get. */
	unsigned int rate = 0;
	bool success = false;
	for (int i = 0; cw_supported_sample_rates[i]; i++) {
		rate = cw_supported_sample_rates[i];
		if (!ioctl(*fd, (int) SNDCTL_DSP_SPEED, &rate)) {
			if (rate != cw_supported_sample_rates[i]) {
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING, "cw_oss: imprecise sample rate:");
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING, "cw_oss: asked for: %u", cw_supported_sample_rates[i]);
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING, "cw_oss: got:       %u", rate);
			}
			success = true;
			break;
		}
	}

	if (!success) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_SPEED): \"%s\"", strerror(errno));
		return CW_FAILURE;
        } else {
		*sample_rate = rate;
	}


	audio_buf_info buff;
	if (ioctl(*fd, (int) SNDCTL_DSP_GETOSPACE, &buff) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_GETOSPACE): \"%s\"", strerror(errno));
		return CW_FAILURE;
        } else {
		/*
		fprintf(stderr, "before:\n");
		fprintf(stderr, "buff.fragments = %d\n", buff.fragments);
		fprintf(stderr, "buff.fragsize = %d\n", buff.fragsize);
		fprintf(stderr, "buff.bytes = %d\n", buff.bytes);
		fprintf(stderr, "buff.fragstotal = %d\n", buff.fragstotal);
		*/
	}


#if CW_OSS_SET_FRAGMENT
	/*
	 * Live a little dangerously, by trying to set the fragment size of the
	 * card.  We'll try for a relatively short fragment of 128 bytes.  This
	 * gives us a little better granularity over the amounts of audio data
	 * we write periodically to the soundcard output buffer.  We may not get
	 * the requested fragment size, and may be stuck with the default.  The
	 * argument has the format 0xMMMMSSSS - fragment size is 2^SSSS, and
	 * setting 0x7fff for MMMM allows as many fragments as the driver can
	 * support.
	 */
	/* parameter = 0x7fff << 16 | CW_OSS_SETFRAGMENT; */
	parameter = 0x0032 << 16 | CW_OSS_SETFRAGMENT;

	if (ioctl(*fd, (int) SNDCTL_DSP_SETFRAGMENT, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_SETFRAGMENT): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO,
		      "cw_oss: fragment size is 2^%d = %d", parameter & 0x0000ffff, 2 << ((parameter & 0x0000ffff) - 1));

	/* Query fragment size just to get the driver buffers set. */
	if (ioctl(*fd, (int) SNDCTL_DSP_GETBLKSIZE, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_GETBLKSIZE): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }

	if (parameter != (1 << CW_OSS_SETFRAGMENT)) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: OSS fragment size not set, %d", parameter);
        }

#endif
#if CW_OSS_SET_POLICY
	parameter = 5;
	if (ioctl(*fd, SNDCTL_DSP_POLICY, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_DSP_POLICY): \"%s\"", strerror(errno));
		return CW_FAILURE;
        }
#endif

	if (ioctl(*fd, (int) SNDCTL_DSP_GETOSPACE, &buff) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl(SNDCTL_GETOSPACE): \"%s\"", strerror(errno));
		return CW_FAILURE;
        } else {
		/*
		fprintf(stderr, "after:\n");
		fprintf(stderr, "buff.fragments = %d\n", buff.fragments);
		fprintf(stderr, "buff.fragsize = %d\n", buff.fragsize);
		fprintf(stderr, "buff.bytes = %d\n", buff.bytes);
		fprintf(stderr, "buff.fragstotal = %d\n", buff;3R.fragstotal);
		*/
	}

	return CW_SUCCESS;
}





/**
   \brief Close OSS device associated with current generator
*/
void cw_oss_close_device_internal(cw_gen_t *gen)
{
	close(gen->audio_sink);
	gen->audio_sink = -1;
	gen->audio_device_is_open = false;

#if CW_DEV_RAW_SINK
	if (gen->dev_raw_sink != -1) {
		close(gen->dev_raw_sink);
		gen->dev_raw_sink = -1;
	}
#endif
	return;
}





int cw_oss_get_version_internal(int fd, int *x, int *y, int *z)
{
	assert (fd);

	int parameter = 0;
	if (ioctl(fd, (int) OSS_GETVERSION, &parameter) == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "cw_oss: ioctl OSS_GETVERSION");
		return CW_FAILURE;
        } else {
		*x = (parameter & 0xFF0000) >> 16;
		*y = (parameter & 0x00FF00) >> 8;
		*z = (parameter & 0x0000FF) >> 0;
		return CW_SUCCESS;
	}
}





#else /* #ifdef LIBCW_WITH_OSS */




#include <stdbool.h>
#include "libcw_oss.h"





bool cw_is_oss_possible(__attribute__((unused)) const char *device)
{
	return false;
}





int  cw_oss_configure(__attribute__((unused)) cw_gen_t *gen, __attribute__((unused)) const char *device)
{
	return CW_FAILURE;
}





#endif /* #ifdef LIBCW_WITH_OSS */
