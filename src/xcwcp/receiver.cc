// Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
// Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include "config.h"

#include <cstdlib>
#include <cerrno>
#include <string>
#include <sstream>

#include <cstdio>

#include "receiver.h"
#include "display.h"
#include "modeset.h"

#include "libcw.h"

#include "i18n.h"

namespace cw {


//-----------------------------------------------------------------------
//  Class Receiver
//-----------------------------------------------------------------------

// poll()
//
// Poll the CW library receive buffer for a complete character, and handle
// anything found in it.
void Receiver::poll(const Mode *current_mode)
{
	if (!current_mode->is_receive()) {
		return;
	}

	// Report and clear any receiver errors noted when handling
	// the last libcw keyer event.
	if (libcw_receive_errno_ != 0) {
		poll_report_receive_error();
	}

	// If we are awaiting a possible inter-word space,
	// poll that first, then go on to poll receive
	// characters; otherwise just poll receive characters.
	if (is_pending_inter_word_space_) {

		// This call directly asks receiver: "did you
		// record space after a character that is long
		// enough to treat it as end of word?".
		poll_receive_space();

		// If we received a space, poll the next
		// possible receive character
		if (!is_pending_inter_word_space_) {
			poll_receive_character();
		}
	} else {
		// Not awaiting a possible space, so just poll
		// the next possible receive character.
		poll_receive_character();
	}

	return;
}





// handle_key_event()
//
// Specific handler for receive mode key events.  Handles both press and
// release events, but ignores autorepeat.
void Receiver::handle_key_event(QKeyEvent *event, const Mode *current_mode,
				bool is_reverse_paddles)
{
	if (!current_mode->is_receive()) {
		return;
	}

	//fprintf(stderr, "\n\n");

	// If this is a key press that is not the first one of an
	// autorepeating key, ignore the event.  This prevents
	// autorepeat from getting in the way of identifying the real
	// keyboard events we are after.
	if (event->isAutoRepeat()) {
		return;
	}

	if (event->type() == QEvent::KeyPress
	    || event->type() == QEvent::KeyRelease) {

		const int is_down = event->type() == QEvent::KeyPress;

		if (event->key() == Qt::Key_Space
		    || event->key() == Qt::Key_Up
		    || event->key() == Qt::Key_Down
		    || event->key() == Qt::Key_Enter
		    || event->key() == Qt::Key_Return) {

			// These keys are obvious candidates for
			// "straight key" key.

			// Prepare timestamp for libcw on both "key
			// up" and "key down" events. There is no code
			// in libcw that would generate updated
			// consecutive timestamps for us (as it does
			// in case of iambic keyer).
			gettimeofday(&timer, NULL);
			//fprintf(stderr, "time on Skey down:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);

			cw_notify_straight_key_event(is_down);

			event->accept();

		} else if (event->key() == Qt::Key_Left) {

			// Notice that keyboard keys that are
			// recognized as iambic keyer paddles, and
			// left/right mouse buttons that are also
			// treated as iambic keyer paddles, are
			// handled by basically the same code. I was
			// thinking about putting the common code in
			// function, but I didn't do it because of
			// performance (whether I'm right here or
			// wrong that's another thing).

			is_left_down = is_down;
			if (is_left_down && !is_right_down) {
				// Prepare timestamp for libcw, but
				// only for initial "paddle down"
				// event at the beginning of
				// character. Don't create the
				// timestamp for any successive
				// "paddle down" events inside a
				// character.
				//
				// In case of iambic keyer the
				// timestamps for every next
				// (non-initial) "paddle up" or
				// "paddle down" event in a character
				// will be created by libcw.
				gettimeofday(&timer, NULL);
				//fprintf(stderr, "time on Lkey down:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);
			}

			// Inform libcw about state of left paddle
			// regardless of state of the other paddle.
			is_reverse_paddles
				? cw_notify_keyer_dash_paddle_event(is_down)
				: cw_notify_keyer_dot_paddle_event(is_down);

			event->accept();

		} else if (event->key() == Qt::Key_Right) {

			is_right_down = is_down;
			if (is_right_down && !is_left_down) {
				// Prepare timestamp for libcw, but
				// only for initial "paddle down"
				// event at the beginning of
				// character. Don't create the
				// timestamp for any successive
				// "paddle down" events inside a
				// character.
				//
				// In case of iambic keyer the
				// timestamps for every next
				// (non-initial) "paddle up" or
				// "paddle down" event in a character
				// will be created by libcw.
				gettimeofday(&timer, NULL);
				//fprintf(stderr, "time on Rkey down:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);
			}

			// If this is the RightArrow key, use as the
			// other one of the paddles.

			// Inform libcw about state of left paddle
			// regardless of state of the other paddle.
			is_reverse_paddles
				? cw_notify_keyer_dot_paddle_event(is_down)
				: cw_notify_keyer_dash_paddle_event(is_down);

			event->accept();

		} else {
			// Some other, uninteresting key. Ignore it.
			;
		}
	}

	return;
}





// handle_mouse_event()
//
// Specific handler for receive mode key events.  Handles button press and
// release events, folds doubleclick into press, and ignores mouse moves.
void Receiver::handle_mouse_event(QMouseEvent *event, const Mode *current_mode,
				  bool is_reverse_paddles)
{
	if (!current_mode->is_receive()) {
		return;
	}

	//fprintf(stderr, "\n\n");


	if (event->type() == QEvent::MouseButtonPress
	    || event->type() == QEvent::MouseButtonDblClick
	    || event->type() == QEvent::MouseButtonRelease) {

		const int is_down = event->type() == QEvent::MouseButtonPress
			|| event->type() == QEvent::MouseButtonDblClick;

		// If this is the Middle button, use as a straight key.
		if (event->button() == Qt::MidButton) {

			// Prepare timestamp for libcw on both "key
			// up" and "key down" events. There is no code
			// in libcw that would generate updated
			// consecutive timestamps for us (as it does
			// in case of iambic keyer).
			gettimeofday(&timer, NULL);
			//fprintf(stderr, "time on Skey down:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);

			cw_notify_straight_key_event(is_down);

			event->accept();

		} else if (event->button() == Qt::LeftButton) {

			// Notice that keyboard keys that are
			// recognized as iambic keyer paddles, and
			// left/right mouse buttons that are also
			// treated as iambic keyer paddles, are
			// handled by basically the same code. I was
			// thinking about putting the common code in
			// function, but I didn't do it because of
			// performance (whether I'm right here or
			// wrong that's another thing).

			is_left_down = is_down;
			if (is_left_down && !is_right_down) {
				// Prepare timestamp for libcw, but
				// only for initial "paddle down"
				// event at the beginning of
				// character. Don't create the
				// timestamp for any successive
				// "paddle down" events inside a
				// character.
				//
				// In case of iambic keyer the
				// timestamps for every next
				// (non-initial) "paddle up" or
				// "paddle down" event in a character
				// will be created by libcw.
				gettimeofday(&timer, NULL);
				//fprintf(stderr, "time on Lkey down:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);
			}

			// Inform libcw about state of left paddle
			// regardless of state of the other paddle.
			is_reverse_paddles
				? cw_notify_keyer_dash_paddle_event(is_down)
				: cw_notify_keyer_dot_paddle_event(is_down);

			event->accept();

		} else if (event->button() == Qt::RightButton) {

			is_right_down = is_down;
			if (is_right_down && !is_left_down) {
				// Prepare timestamp for libcw, but
				// only for initial "paddle down"
				// event at the beginning of
				// character. Don't create the
				// timestamp for any successive
				// "paddle down" events inside a
				// character.
				//
				// In case of iambic keyer the
				// timestamps for every next
				// (non-initial) "paddle up" or
				// "paddle down" event in a character
				// will be created by libcw.
				gettimeofday(&timer, NULL);
				//fprintf(stderr, "time on Rkey down:  %10ld : %10ld\n", timer.tv_sec, timer.tv_usec);
			}

			// Inform libcw about state of right paddle
			// regardless of state of the other paddle.
			is_reverse_paddles
				? cw_notify_keyer_dot_paddle_event(is_down)
				: cw_notify_keyer_dash_paddle_event(is_down);

			event->accept();

		} else {
			// Some other mouse button, or mouse cursor
			// movement. Ignore it.
			;
		}
	}

	return;
}





// handle_libcw_keying_event()
//
// Handler for the keying callback from the CW library indicating that the
// keying state changed.  The function handles the receive of keyed CW,
// ignoring calls on non-receive modes.
//
// This function is called in signal handler context, and it takes care to
// call only functions that are safe within that context.  In particular,
// it goes out of its way to deliver results by setting flags that are
// later handled by receive polling.
void Receiver::handle_libcw_keying_event(struct timeval *t, int key_state)
{
	// Ignore calls where the key state matches our tracked key state.  This
	// avoids possible problems where this event handler is redirected between
	// application instances; we might receive an end of tone without seeing
	// the start of tone.
	if (key_state == tracked_key_state_) {
		//fprintf(stderr, "tracked key state == %d\n", tracked_key_state_);
		return;
	} else {
		//fprintf(stderr, "tracked key state := %d\n", key_state);
		tracked_key_state_ = key_state;
	}

	// If this is a tone start and we're awaiting an inter-word
	// space, cancel that wait and clear the receive buffer.
	if (key_state && is_pending_inter_word_space_) {
		// Tell receiver to prepare (to make space) for
		// receiving new character.
		cw_clear_receive_buffer();

		// The tone start means that we're seeing the next
		// incoming character within the same word, so no
		// inter-word space is possible at this point in
		// time. The space that we were observing/waiting for,
		// was just inter-character space.
		is_pending_inter_word_space_ = false;
	}

	//fprintf(stderr, "calling callback, stage 2\n");

	// Pass tone state on to the library.  For tone end, check to
	// see if the library has registered any receive error.
	if (key_state) {
		// Key down
		//fprintf(stderr, "start receive tone: %10ld . %10ld\n", t->tv_sec, t->tv_usec);
		if (!cw_start_receive_tone(t)) {
			perror("cw_start_receive_tone");
			abort();
		}
	} else {
		// Key up
		//fprintf(stderr, "end receive tone:   %10ld . %10ld\n", t->tv_sec, t->tv_usec);
		if (!cw_end_receive_tone(t)) {
			// Handle receive error detected on tone end.  For
			// ENOMEM and ENOENT we set the error in a class
			// flag, and display the appropriate message on the
			// next receive poll.
			switch (errno) {
			case EAGAIN:
				// libcw treated the tone as noise (it was
				// shorter than noise threshold).
				// No problem, not an error.
				break;

			case ENOMEM:
			case ENOENT:
				libcw_receive_errno_ = errno;
				cw_clear_receive_buffer();
				break;

			default:
				perror("cw_end_receive_tone");
				abort();
			}
		}
	}

	return;
}





// clear()
//
// Clear the library receive buffer and our own flags.
void Receiver::clear()
{
	cw_clear_receive_buffer();
	is_pending_inter_word_space_ = false;
	libcw_receive_errno_ = 0;
	tracked_key_state_ = FALSE;

	return;
}





// poll_report_receive_error()
//
// Handle any error registered when handling a libcw keying event.
void Receiver::poll_report_receive_error()
{
	// Handle any receive errors detected on tone end but delayed until here.
	display_->show_status(libcw_receive_errno_ == ENOENT
			      ? _("Badly formed CW element")
			      : _("Receive buffer overrun"));

	libcw_receive_errno_ = 0;

	return;
}





// poll_receive_character()
//
// Receive any new character from the CW library.
void Receiver::poll_receive_character()
{
	char c;

	// Don't use receiver.timer - it is used eclusively for
	// marking initial "key down" events. Use local throw-away
	// timer2.
	//
	// Additionally using reveiver.timer here would mess up time
	// intervals measured by receiver.timer, and that would
	// interfere with recognizing dots and dashes.
	struct timeval timer2;
	gettimeofday(&timer2, NULL);
	//fprintf(stderr, "poll_receive_char:  %10ld : %10ld\n", timer2.tv_sec, timer2.tv_usec);
	if (cw_receive_character(&timer2, &c, NULL, NULL)) {
		// Receiver stores full, well formed
		// character. Display it.
		display_->append(c);

		// A full character has been received. Directly after
		// it comes a space. Eiter a short inter-character
		// space followed by another character (in this case
		// we won't display the inter-character space), or
		// longer inter-word space - this space we would like
		// to catch and display.
		//
		// Set a flag indicating that next poll may result in
		// inter-word space.
		is_pending_inter_word_space_ = true;

		// Update the status bar to show the character
		// received.  Put the received char at the end of
		// string to avoid "jumping" of whole string when
		// width of glyph of received char changes at variable
		// font width.
		QString status = _("Received at %1 WPM: '%2'");
		display_->show_status(status.arg(cw_get_receive_speed()).arg(c));
		//fprintf(stderr, "Received character '%c'\n", c);

	} else {
		// Handle receive error detected on trying to read a character.
		switch (errno) {
		case EAGAIN:
			// Call made too early, receiver hasn't
			// received a full character yet. Try next time.
			break;

		case ERANGE:
			// Call made not in time, or not in proper
			// sequence. Receiver hasn't received any
			// character (yet). Try harder.
			break;

		case ENOENT:
			// Invalid character in receiver's buffer.
			{	// New scope to avoid gcc 3.2.2 internal compiler error
				cw_clear_receive_buffer();
				display_->append('?');

				QString status = _("Unknown character received at %1 WPM");
				display_->show_status(status.arg(cw_get_receive_speed()));
			}
			break;

		default:
			perror("cw_receive_character");
			abort();
		}
	}

	return;
}





// poll_receive_space()
//
// If we received a character on an earlier poll, check again to see if we
// need to revise the decision about whether it is the end of a word too.
void Receiver::poll_receive_space()
{
	// Recheck the receive buffer for end of word.
	bool is_end_of_word;

	// We expect the receiver to contain a character, but we don't
	// ask for it this time. The receiver should also store
	// information about a post-character space. If it is longer
	// than a regular inter-character space, then the receiver
	// will treat it as inter-word space, and communicate it over
	// is_end_of_word.

	// Don't use receiver.timer - it is used eclusively for
	// marking initial "key down" events. Use local throw-away
	// timer2.
	struct timeval timer2;
	gettimeofday(&timer2, NULL);
	//fprintf(stderr, "poll_receive_space: %10ld : %10ld\n", timer2.tv_sec, timer2.tv_usec);
	cw_receive_character(&timer2, NULL, &is_end_of_word, NULL);
	if (is_end_of_word) {
		//fprintf(stderr, "End of word\n\n");
		display_->append(' ');
		cw_clear_receive_buffer();
		is_pending_inter_word_space_ = false;
	} else {
		// We don't reset is_pending_inter_word_space. The
		// space that currently lasts, and isn't long enough
		// to be considered inter-word space, may grow to
		// become the inter-word space. Or not.
		//
		// This growing of inter-character space into
		// inter-word space may be terminated by incoming next
		// tone (key down event) - the tone will mark
		// beginning of new character within the same
		// word. And since a new character begins, the flag
		// will be reset (elsewhere).
	}

	return;
}

}  // cw namespace
