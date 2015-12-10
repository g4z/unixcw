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
   \file libcw_pa.c

   \brief PulseAudio audio sink.
*/





#include "config.h"


#ifdef LIBCW_WITH_PULSEAUDIO



#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h> /* dlopen() and related symbols */
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "libcw.h"
#include "libcw_pa.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_gen.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;


static pa_simple *cw_pa_simple_new_internal(pa_sample_spec *ss, pa_buffer_attr *ba, const char *device, const char *stream_name, int *error);
static int        cw_pa_dlsym_internal(void *handle);
static int        cw_pa_open_device_internal(cw_gen_t *gen);
static void       cw_pa_close_device_internal(cw_gen_t *gen);
static int        cw_pa_write_internal(cw_gen_t *gen);



static struct {
	void *handle;

	pa_simple *(* pa_simple_new)(const char *server, const char *name, pa_stream_direction_t dir, const char *dev, const char *stream_name, const pa_sample_spec *ss, const pa_channel_map *map, const pa_buffer_attr *attr, int *error);
	void       (* pa_simple_free)(pa_simple *s);
	int        (* pa_simple_write)(pa_simple *s, const void *data, size_t bytes, int *error);
	pa_usec_t  (* pa_simple_get_latency)(pa_simple *s, int *error);
	int        (* pa_simple_drain)(pa_simple *s, int *error);

	size_t     (* pa_usec_to_bytes)(pa_usec_t t, const pa_sample_spec *spec);
	char      *(* pa_strerror)(int error);
} cw_pa = {
	.handle = NULL,

	.pa_simple_new = NULL,
	.pa_simple_free = NULL,
	.pa_simple_write = NULL,
	.pa_simple_get_latency = NULL,
	.pa_simple_drain = NULL,

	.pa_usec_to_bytes = NULL,
	.pa_strerror = NULL
};




static const pa_sample_format_t CW_PA_SAMPLE_FORMAT = PA_SAMPLE_S16LE; /* Signed 16 bit, Little Endian */
static const int CW_PA_BUFFER_N_SAMPLES = 1024;





/**
   \brief Check if it is possible to open PulseAudio output

   Function first tries to load PulseAudio library, and then does a test
   opening of PulseAudio output, but it closes it before returning.

   \param device - sink device, NULL for default PulseAudio device

   \return true if opening PulseAudio output succeeded;
   \return false if opening PulseAudio output failed;
*/
bool cw_is_pa_possible(const char *device)
{
	const char *library_name = "libpulse-simple.so";
	if (!cw_dlopen_internal(library_name, &(cw_pa.handle))) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_pa: can't access PulseAudio library \"%s\"", library_name);
		return false;
	}

	int rv = cw_pa_dlsym_internal(cw_pa.handle);
	if (rv < 0) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_pa: failed to resolve PulseAudio symbol #%d, can't correctly load PulseAudio library", rv);
		dlclose(cw_pa.handle);
		return false;
	}

	const char *dev = (char *) NULL;
	if (device && strcmp(device, CW_DEFAULT_PA_DEVICE)) {
		dev = device;
	}

	pa_sample_spec ss;
	pa_buffer_attr ba;
	int error = 0;

	pa_simple *s = cw_pa_simple_new_internal(&ss, &ba, dev, "cw_is_pa_possible()", &error);

	if (!s) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_pa: can't connect to PulseAudio server: %s", cw_pa.pa_strerror(error));
		if (cw_pa.handle) {
			dlclose(cw_pa.handle);
		}
		return false;
	} else {
		cw_pa.pa_simple_free(s);
		s = NULL;
		return true;
	}
}





int cw_pa_configure(cw_gen_t *gen, const char *dev)
{
	assert (gen);

	gen->audio_system = CW_AUDIO_PA;
	cw_gen_set_audio_device_internal(gen, dev);

	gen->open_device  = cw_pa_open_device_internal;
	gen->close_device = cw_pa_close_device_internal;
	gen->write        = cw_pa_write_internal;

	return CW_SUCCESS;
}





int cw_pa_write_internal(cw_gen_t *gen)
{
	assert (gen);
	assert (gen->audio_system == CW_AUDIO_PA);

	int error = 0;
	size_t n_bytes = sizeof (gen->buffer[0]) * gen->buffer_n_samples;
	int rv = cw_pa.pa_simple_write(gen->pa_data.s, gen->buffer, n_bytes, &error);
	if (rv < 0) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_pa: pa_simple_write() failed: %s", cw_pa.pa_strerror(error));
	} else {
		//cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_INFO, "libcw_pa: written %d samples with PulseAudio", gen->buffer_n_samples);
	}

	return CW_SUCCESS;

}





/**
   \brief Wrapper for pa_simple_new()

   Wrapper for pa_simple_new() and related code. The code block contained
   in the function is useful in two different places: when first probing
   if PulseAudio output is available, and when opening PulseAudio output
   for writing.

   On success the function returns pointer to PulseAudio sink open for
   writing (playback). The function tries to set up buffering parameters
   for minimal latency, but it doesn't try too hard.

   The function *does not* set size of audio buffer in libcw's generator.

   \param ss - sample spec data, non-NULL pointer to variable owned by caller
   \param ba - buffer attributes data, non-NULL pointer to variable owned by caller
   \param device - name of PulseAudio device to be used, or NULL for default device
   \param stream_name - descriptive name of client, passed to pa_simple_new
   \param error - output, pointer to variable storing potential PulseAudio error code

   \return pointer to new PulseAudio sink on success
   \return NULL on failure
*/
pa_simple *cw_pa_simple_new_internal(pa_sample_spec *ss, pa_buffer_attr *ba, const char *device, const char *stream_name, int *error)
{
	ss->format = CW_PA_SAMPLE_FORMAT;
	ss->rate = 44100;
	ss->channels = 1;

	const char *dev = (char *) NULL; /* NULL - let PulseAudio use default device */
	if (device && strcmp(device, CW_DEFAULT_PA_DEVICE)) {
		dev = device; /* non-default device */
	}

	// http://www.mail-archive.com/pulseaudio-tickets@mail.0pointer.de/msg03295.html
	ba->tlength = cw_pa.pa_usec_to_bytes(50*1000, ss);
	ba->minreq = cw_pa.pa_usec_to_bytes(0, ss);
	ba->maxlength = cw_pa.pa_usec_to_bytes(50*1000, ss);
	/* ba->prebuf = ; */ /* ? */
	/* ba->fragsize = sizeof(uint32_t) -1; */ /* not relevant to playback */

	pa_simple *s = cw_pa.pa_simple_new(NULL,                  /* server name (NULL for default) */
					   "libcw",               /* descriptive name of client (application name etc.) */
					   PA_STREAM_PLAYBACK,    /* stream direction */
					   dev,                   /* device/sink name (NULL for default) */
					   stream_name,           /* stream name, descriptive name for this client (application name, song title, etc.) */
					   ss,                    /* sample specification */
					   NULL,                  /* channel map (NULL for default) */
					   ba,                    /* buffering attributes (NULL for default) */
					   error);                /* error buffer (when routine returns NULL) */

	return s;
}





/**
   \brief Resolve/get symbols from PulseAudio library

   Function resolves/gets addresses of few PulseAudio functions used by
   libcw and stores them in cw_pa global variable.

   On failure the function returns negative value, different for every
   symbol that the funciton failed to resolve. Function stops and returns
   on first failure.

   \param handle - handle to open PulseAudio library

   \return 0 on success
   \return negative value on failure
*/
int cw_pa_dlsym_internal(void *handle)
{
	*(void **) &(cw_pa.pa_simple_new)         = dlsym(handle, "pa_simple_new");
	if (!cw_pa.pa_simple_new)         return -1;
	*(void **) &(cw_pa.pa_simple_free)        = dlsym(handle, "pa_simple_free");
	if (!cw_pa.pa_simple_free)        return -2;
	*(void **) &(cw_pa.pa_simple_write)       = dlsym(handle, "pa_simple_write");
	if (!cw_pa.pa_simple_write)       return -3;
	*(void **) &(cw_pa.pa_strerror)           = dlsym(handle, "pa_strerror");
	if (!cw_pa.pa_strerror)           return -4;
	*(void **) &(cw_pa.pa_simple_get_latency) = dlsym(handle, "pa_simple_get_latency");
	if (!cw_pa.pa_simple_get_latency) return -5;
	*(void **) &(cw_pa.pa_simple_drain)       = dlsym(handle, "pa_simple_drain");
	if (!cw_pa.pa_simple_drain)       return -6;
	*(void **) &(cw_pa.pa_usec_to_bytes)      = dlsym(handle, "pa_usec_to_bytes");
	if (!cw_pa.pa_usec_to_bytes)       return -7;

	return 0;
}





/**
   \brief Open PulseAudio output, associate it with given generator

   You must use cw_gen_set_audio_device_internal() before calling
   this function. Otherwise generator \p gen won't know which device to open.

   \param gen - current generator

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_pa_open_device_internal(cw_gen_t *gen)
{
	const char *dev = (char *) NULL; /* NULL - let PulseAudio use default device */
	if (gen->audio_device && strcmp(gen->audio_device, CW_DEFAULT_PA_DEVICE)) {
		dev = gen->audio_device; /* non-default device */
	}

	int error = 0;
	gen->pa_data.s = cw_pa_simple_new_internal(&gen->pa_data.ss, &gen->pa_data.ba,
						   dev,
						   gen->client.name ? gen->client.name : "app",
						   &error);

 	if (!gen->pa_data.s) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_pa: can't connect to PulseAudio server: %s", cw_pa.pa_strerror(error));
		return false;
	}

	gen->buffer_n_samples = CW_PA_BUFFER_N_SAMPLES;
	gen->sample_rate = gen->pa_data.ss.rate;

	if ((gen->pa_data.latency_usecs = cw_pa.pa_simple_get_latency(gen->pa_data.s, &error)) == (pa_usec_t) -1) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw_pa: pa_simple_get_latency() failed: %s", cw_pa.pa_strerror(error));
	}

#if CW_DEV_RAW_SINK
	gen->dev_raw_sink = open("/tmp/cw_file.pa.raw", O_WRONLY | O_TRUNC | O_NONBLOCK);
#endif
	assert (gen && gen->pa_data.s);

	return CW_SUCCESS;
}





/**
   \brief Close PulseAudio device associated with current generator
*/
void cw_pa_close_device_internal(cw_gen_t *gen)
{
	if (gen->pa_data.s) {
		/* Make sure that every single sample was played */
		int error;
		if (cw_pa.pa_simple_drain(gen->pa_data.s, &error) < 0) {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
				      "libcw_pa: pa_simple_drain() failed: %s", cw_pa.pa_strerror(error));
		}
		cw_pa.pa_simple_free(gen->pa_data.s);
		gen->pa_data.s = NULL;
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_WARNING,
			      "libcw_pa: called the function for NULL PA sink");
	}

	if (cw_pa.handle) {
		dlclose(cw_pa.handle);
	}

#if CW_DEV_RAW_SINK
	if (gen->dev_raw_sink != -1) {
		close(gen->dev_raw_sink);
		gen->dev_raw_sink = -1;
	}
#endif
	return;
}





#else /* #ifdef LIBCW_WITH_PULSEAUDIO */





#include <stdbool.h>
#include "libcw_pa.h"





bool cw_is_pa_possible(__attribute__((unused)) const char *device)
{
	return false;
}





int cw_pa_configure(__attribute__((unused)) cw_gen_t *gen, __attribute__((unused)) const char *device)
{
	return CW_FAILURE;
}





#endif /* #ifdef LIBCW_WITH_PULSEAUDIO */
