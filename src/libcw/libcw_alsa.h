/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_ALSA
#define H_LIBCW_ALSA





#include "config.h"





#ifdef LIBCW_WITH_ALSA

#include <alsa/asoundlib.h>

typedef struct cw_alsa_data_struct {
	snd_pcm_t *handle; /* Output handle for audio data. */
} cw_alsa_data_t;


#endif /* #ifdef LIBCW_WITH_ALSA */





#include "libcw_gen.h"

int  cw_alsa_configure(cw_gen_t *gen, const char *device);
void cw_alsa_drop(cw_gen_t *gen);





#endif /* #ifndef H_LIBCW_ALSA */
