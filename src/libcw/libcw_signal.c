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
   \file libcw_signal.c

   \brief Signal handling routines.

   There are some static variables in this file, maybe they should be
   moved to some common structure. I've noticed that these functions
   are used in libcw_gen, libcw_tq and libcw_key. Perhaps these static
   variables should be members of cw_gen_t.
*/





#include "config.h"


#include <signal.h>
#include <errno.h>
#include <stdlib.h>


#include "libcw.h"
#include "libcw_signal.h"
#include "libcw_debug.h"
#include "libcw_gen.h"
#include "libcw_utils.h"


#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif





/* http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=403043 */
#if defined(NSIG)             /* Debian GNU/Linux: signal.h; Debian kFreeBSD: signal.h (libc0.1-dev_2.13-21_kfreebsd-i386.deb) */
#define CW_SIG_MAX (NSIG)
#elif defined(_NSIG)          /* Debian GNU/Linux: asm-generic/signal.h; Debian kFreeBSD: i386-kfreebsd-gnu/bits/signum.h->signal.h (libc0.1-dev_2.13-21_kfreebsd-i386.deb) */
#define CW_SIG_MAX (_NSIG)
#elif defined(RTSIG_MAX)      /* Debian GNU/Linux: linux/limits.h */
#define CW_SIG_MAX ((RTSIG_MAX)+1)
#elif defined(__FreeBSD__)
#define CW_SIG_MAX (_SIG_MAXSIG)
#else
#error "unknown number of signals"
#endif





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;


extern cw_gen_t *cw_generator;





static int  cw_timer_run_internal(int usecs);
static void cw_sigalrm_handlers_caller_internal(int signal_number);
static int  cw_sigalrm_block_internal(bool block);
static void cw_signal_main_handler_internal(int signal_number);





/* The library keeps a single central non-sparse list of SIGALRM signal
   handlers. The handler functions will be called sequentially on each
   SIGALRM received. */
enum { CW_SIGALRM_HANDLERS_MAX = 32 };
static void (*cw_sigalrm_handlers[CW_SIGALRM_HANDLERS_MAX])(void);


/* Flag to tell us if the SIGALRM handler is installed, and a place to
   keep the old SIGALRM disposition, so we can restore it when the
   library decides it can stop handling SIGALRM for a while.  */
static bool cw_is_sigalrm_handlers_caller_installed = false;
static struct sigaction cw_sigalrm_original_disposition;





/**
   \brief Call handlers of SIGALRM signal

   This function calls the SIGALRM signal handlers of the library
   subsystems, expecting them to ignore unexpected calls.

   The handlers are kept in cw_sigalrm_handlers[] table, and can be added
   to the table with cw_timer_run_with_handler_internal().

   SIGALRM is sent to a process every time an itimer timer expires.
   The timer is set with cw_timer_run_internal().
*/
void cw_sigalrm_handlers_caller_internal(__attribute__((unused)) int signal_number)
{
	/* Call the known functions that are interested in SIGALRM signal.
	   Stop on the first free slot found; valid because the array is
	   filled in order from index 0, and there are no deletions. */
	for (int handler = 0;
	     handler < CW_SIGALRM_HANDLERS_MAX && cw_sigalrm_handlers[handler]; handler++) {

		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_INTERNAL, CW_DEBUG_DEBUG,
			      "libcw: SIGALRM handler #%d", handler);

		(cw_sigalrm_handlers[handler])();
	}

	return;
}





/**
   \brief Set up a timer for specified number of microseconds

   Convenience function to set the itimer for a single shot timeout after
   a given number of microseconds. SIGALRM is sent to caller process when the
   timer expires.

   \param usecs - time in microseconds

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_timer_run_internal(int usecs)
{
	struct itimerval itimer;

	/* Set up a single shot timeout for the given period. */
	itimer.it_interval.tv_sec = 0;
	itimer.it_interval.tv_usec = 0;
	itimer.it_value.tv_sec = usecs / CW_USECS_PER_SEC;
	itimer.it_value.tv_usec = usecs % CW_USECS_PER_SEC;
	int status = setitimer(ITIMER_REAL, &itimer, NULL);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: setitimer(%d): %s", usecs, strerror(errno));
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
   \brief Register SIGALRM handler(s), and send SIGALRM signal

   Install top level handler of SIGALRM signal (cw_sigalrm_handlers_caller_internal())
   if it is not already installed.

   Register given \p sigalrm_handler lower level handler, if not NULL and
   if not yet registered.
   Then send SIGALRM signal after delay equal to \p usecs microseconds.

   \param usecs - time for itimer
   \param sigalrm_handler - SIGALRM handler to register

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_timer_run_with_handler_internal(int usecs, void (*sigalrm_handler)(void))
{
	if (!cw_sigalrm_install_top_level_handler_internal()) {
		return CW_FAILURE;
	}

	/* If it's not already present, and one was given, add address
	   of the lower level SIGALRM handler to the list of known
	   handlers. */
	if (sigalrm_handler) {
		int handler;

		/* Search for this handler, or the first free entry,
		   stopping at the last entry in the table even if it's
		   not a match and not free. */
		for (handler = 0; handler < CW_SIGALRM_HANDLERS_MAX - 1; handler++) {
			if (!cw_sigalrm_handlers[handler]
			    || cw_sigalrm_handlers[handler] == sigalrm_handler) {

				break;
			}
		}

		/* If the handler is already there, do no more.  Otherwise,
		   if we ended the search at an unused entry, add it to
		   the list of lower level handlers. */
		if (cw_sigalrm_handlers[handler] != sigalrm_handler) {
			if (cw_sigalrm_handlers[handler]) {
				errno = ENOMEM;
				cw_debug_msg ((&cw_debug_object), CW_DEBUG_INTERNAL, CW_DEBUG_ERROR,
					      "libcw: overflow cw_sigalrm_handlers");
				return CW_FAILURE;
			} else {
				cw_sigalrm_handlers[handler] = sigalrm_handler;
			}
		}
	}

	/* The fact that we receive a call means that something is using
	   timeouts and sound, so make sure that any pending finalization
	   doesn't happen. */
	cw_finalization_cancel_internal();

	/* Depending on the value of usec, either set an itimer, or send
	   ourselves SIGALRM right away. */
	if (usecs <= 0) {
		/* Send ourselves SIGALRM immediately. */
		if (pthread_kill(cw_generator->thread.id, SIGALRM) != 0) {
	        // if (raise(SIGALRM) != 0) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: raise()");
			return CW_FAILURE;
		}

	} else {
		/* Set the itimer to produce a single interrupt after the
		   given duration. */
		if (!cw_timer_run_internal(usecs)) {
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





int cw_sigalrm_install_top_level_handler_internal(void)
{
	if (!cw_is_sigalrm_handlers_caller_installed) {
		/* Install the main SIGALRM handler routine (a.k.a. top level
		   SIGALRM handler) - a function that calls all registered
		   lower level handlers), and keep the  old information
		   (disposition) so we can put it back when useful to do so. */

		struct sigaction action;
		action.sa_handler = cw_sigalrm_handlers_caller_internal;
		action.sa_flags = SA_RESTART;
		sigemptyset(&action.sa_mask);

		int status = sigaction(SIGALRM, &action, &cw_sigalrm_original_disposition);
		if (status == -1) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: sigaction(): %s", strerror(errno));
			return CW_FAILURE;
		}

		cw_is_sigalrm_handlers_caller_installed = true;
	}
	return CW_SUCCESS;
}





/**
   \brief Uninstall the SIGALRM handler, if installed

   Restores SIGALRM's disposition for the system to the state we found
   it in before we installed our own SIGALRM handler.

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_sigalrm_restore_internal(void)
{
	/* Ignore the call if we haven't installed our handler. */
	if (cw_is_sigalrm_handlers_caller_installed) {
		/* Cancel any pending itimer setting. */
		if (!cw_timer_run_internal(0)) {
			return CW_FAILURE;
		}

		/* Put back the SIGALRM information saved earlier. */
		int status = sigaction(SIGALRM, &cw_sigalrm_original_disposition, NULL);
		if (status == -1) {
			perror ("libcw: sigaction");
			return CW_FAILURE;
		}

		cw_is_sigalrm_handlers_caller_installed = false;
	}

	return CW_SUCCESS;
}





/**
   \brief Check if SIGALRM is currently blocked

   Check the signal mask of the process, and return false, with errno
   set to EDEADLK, if SIGALRM is blocked.
   If function returns true, but errno is set, the function has failed
   to check if SIGALRM is blocked.

   \return true if SIGALRM is currently blocked (errno is zero)
   \return true on errors (errno is set by system call that failed)
   \return false if SIGALRM is currently not blocked
*/
bool cw_sigalrm_is_blocked_internal(void)
{
	sigset_t empty_set, current_set;

	/* Prepare empty set of signals */
	int status = sigemptyset(&empty_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigemptyset(): %s", strerror(errno));
		return true;
	}

	/* Block an empty set of signals to obtain the current mask. */
	status = sigprocmask(SIG_BLOCK, &empty_set, &current_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigprocmask(): %s", strerror(errno));
		return true;
	}

	/* Check that SIGALRM is not blocked in the current mask. */
	if (sigismember(&current_set, SIGALRM)) {
		errno = 0;
		return true;
	} else {
		return false;
	}
}





/**
   \brief Block or unblock SIGALRM signal

   Function blocks or unblocks SIGALRM.
   It may be used to block the signal for the duration of certain
   critical sections, and to unblock the signal afterwards.

   \param block - pass true to block SIGALRM, and false to unblock it

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_sigalrm_block_internal(bool block)
{
	sigset_t set;

	/* Prepare empty set of signals */
	int status = sigemptyset(&set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigemptyset(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Add single signal to the set */
	status = sigaddset(&set, SIGALRM);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigaddset(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Block or unblock SIGALRM for the process using the set of signals */
	status = pthread_sigmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: pthread_sigmask(): %s", strerror(errno));
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
   \brief Block the callback from being called

   Function blocks the callback from being called for a critical section of
   caller code if \p block is true, and unblocks the callback if \p block is
   false.

   Function works by blocking SIGALRM; a block should always be matched by
   an unblock, otherwise the tone queue will suspend forever.

   \param block - pass 1 to block SIGALRM, and 0 to unblock it
*/
void cw_block_callback(int block)
{
	cw_sigalrm_block_internal((bool) block);
	return;
}





/**
   \brief Wait for a signal, usually a SIGALRM

   Function assumes that SIGALRM is not blocked.
   Function may return CW_FAILURE on failure, i.e. when call to
   sigemptyset(), sigprocmask() or sigsuspend() fails.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_signal_wait_internal(void)
{
	sigset_t empty_set, current_set;

	/* Prepare empty set of signals */
	int status = sigemptyset(&empty_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigemptyset(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Block an empty set of signals to obtain the current mask. */
	status = sigprocmask(SIG_BLOCK, &empty_set, &current_set);
	if (status == -1) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: sigprocmask(): %s", strerror(errno));
		return CW_FAILURE;
	}

	/* Wait on the current mask */
	status = sigsuspend(&current_set);
	if (status == -1 && errno != EINTR) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: suspend(): %s", strerror(errno));
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/* Array of callbacks registered for convenience signal handling.
   They're initialized dynamically to SIG_DFL (if SIG_DFL is not NULL,
   which it seems that it is in most cases). */
static void (*cw_signal_callbacks[CW_SIG_MAX])(int);





/**
   \brief Generic function calling signal handlers

   Signal handler function registered when cw_register_signal_handler()
   is called.
   The function resets the library (with cw_complete_reset()), and then,
   depending on value of signal handler for given \p signal_number:
   \li calls exit(EXIT_FAILURE) if signal handler is SIG_DFL, or
   \li continues without further actions if signal handler is SIG_IGN, or
   \li calls the signal handler.

   The signal handler for given \p signal_number is either a pre-set, default
   value, or is a value registered earlier with cw_register_signal_handler().

   \param signal_number
*/
void cw_signal_main_handler_internal(int signal_number)
{
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_FINALIZATION, CW_DEBUG_INFO,
		      "libcw: caught signal %d", signal_number);

	/* Reset the library and retrieve the signal's handler. */
	cw_complete_reset();
	void (*callback_func)(int) = cw_signal_callbacks[signal_number];

	/* The default action is to stop the process; exit(1) seems to cover it. */
	if (callback_func == SIG_DFL) {
		exit(EXIT_FAILURE);
	} else if (callback_func == SIG_IGN) {
		/* continue */
	} else {
		/* invoke any additional handler callback function */
		(*callback_func)(signal_number);
	}

	return;
}





/**
   \brief Register a signal handler and optional callback function for given signal number

   On receipt of that signal, all library features will be reset to their
   default states.  Following the reset, if \p callback_func is a function
   pointer, the function is called; if it is SIG_DFL, the library calls
   exit(); and if it is SIG_IGN, the library returns from the signal handler.

   This is a convenience function for clients that need to clean up library
   on signals, with either exit, continue, or an additional function call;
   in effect, a wrapper round a restricted form of sigaction.

   The \p signal_number argument indicates which signal to catch.

   On problems errno is set to EINVAL if \p signal_number is invalid
   or if a handler is already installed for that signal, or to the
   sigaction error code.

   \param signal_number
   \param callback_func

   \return CW_SUCCESS - if the signal handler installs correctly
   \return CW_FAILURE - on errors or problems
*/
int cw_register_signal_handler(int signal_number, void (*callback_func)(int))
{
	static bool is_initialized = false;

	/* On first call, initialize all signal_callbacks to SIG_DFL. */
	if (!is_initialized) {
		for (int i = 0; i < CW_SIG_MAX; i++) {
			cw_signal_callbacks[i] = SIG_DFL;
		}
		is_initialized = true;
	}

	/* Reject invalid signal numbers, and SIGALRM, which we use internally. */
	if (signal_number < 0
	    || signal_number >= CW_SIG_MAX
	    || signal_number == SIGALRM) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Install our handler as the actual handler. */
	struct sigaction action, original_disposition;
	action.sa_handler = cw_signal_main_handler_internal;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	int status = sigaction(signal_number, &action, &original_disposition);
	if (status == -1) {
		perror("libcw: sigaction");
		return CW_FAILURE;
	}

	/* If we trampled another handler, replace it and return false. */
	if (!(original_disposition.sa_handler == cw_signal_main_handler_internal
	      || original_disposition.sa_handler == SIG_DFL
	      || original_disposition.sa_handler == SIG_IGN)) {

		status = sigaction(signal_number, &original_disposition, NULL);
		if (status == -1) {
			perror("libcw: sigaction");
			return CW_FAILURE;
		}

		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Save the callback function (it may validly be SIG_DFL or SIG_IGN). */
	cw_signal_callbacks[signal_number] = callback_func;

	return CW_SUCCESS;
}





/**
   \brief Unregister a signal handler interception

   Function removes a signal handler interception previously registered
   with cw_register_signal_handler().

   \param signal_number

   \return true if the signal handler uninstalls correctly
   \return false otherwise (with errno set to EINVAL or to the sigaction error code)
*/
int cw_unregister_signal_handler(int signal_number)
{
	/* Reject unacceptable signal numbers. */
	if (signal_number < 0
	    || signal_number >= CW_SIG_MAX
	    || signal_number == SIGALRM) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	/* See if the current handler was put there by us. */
	struct sigaction original_disposition;
	int status = sigaction(signal_number, NULL, &original_disposition);
	if (status == -1) {
		perror("libcw: sigaction");
		return CW_FAILURE;
	}

	if (original_disposition.sa_handler != cw_signal_main_handler_internal) {
		/* No, it's not our signal handler. Don't touch it. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Remove the signal handler by resetting to SIG_DFL. */
	struct sigaction action;
	action.sa_handler = SIG_DFL;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	status = sigaction(signal_number, &action, NULL);
	if (status == -1) {
		perror("libcw: sigaction");
		return CW_FAILURE;
	}

	/* Reset the callback entry for tidiness. */
	cw_signal_callbacks[signal_number] = SIG_DFL;

	return CW_SUCCESS;
}
