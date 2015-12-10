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

#ifndef _XCWCP_RECEIVER_H
#define _XCWCP_RECEIVER_H

#include <QMouseEvent>
#include <QKeyEvent>


//-----------------------------------------------------------------------
//  Class Receiver
//-----------------------------------------------------------------------

// Encapsulates the main application receiver data and functions.  Receiver
// abstracts states associated with receiving, event handling, libcw keyer
// event handling, and data passed between signal handler and foreground
// contexts.

namespace cw {

class Display;
class Mode;

class Receiver {
 public:
	Receiver (Display *display)
		: display_ (display),
		is_pending_inter_word_space_ (false),
		libcw_receive_errno_ (0),
		tracked_key_state_ (false),
		is_left_down (false),
		is_right_down (false) { }

	// Poll timeout handler.
	void poll(const Mode *current_mode);

	// Keypress event handler.
	void handle_key_event(QKeyEvent *event, const Mode *current_mode,
			      bool is_reverse_paddles);

	// Mouse button press event handler.
	void handle_mouse_event(QMouseEvent *event, const Mode *current_mode,
				bool is_reverse_paddles);

	// CW library keying event handler.
	void handle_libcw_keying_event(struct timeval *t, int key_state);

	// Clear out queued data on stop, mode change, etc.
	void clear();

	// Timer for measuring length of dots and dashes.
	//
	// Initial value of the timestamp is created by xcwcp's
	// receiver on first "paddle down" event in a character. The
	// timestamp is then updated by libcw on specific time
	// intervals. The intervals are a function of keyboard key
	// presses or mouse button presses recorded by xcwcp.
	struct timeval timer;

 private:
	// Display used for output.
	Display *display_;

	// Flag indicating if receive polling has received a character, and may need
	// to augment it with a word space on a later poll.
	volatile bool is_pending_inter_word_space_;

	// Flag indicating possible receive errno detected in signal
	// handler context and needing to be passed to the foreground.
	volatile int libcw_receive_errno_;

	// Safety flag to ensure that we keep the library in sync with keyer events.
	// Without, there's a chance that of a on-off event, one half will go to one
	// application instance, and the other to another instance.
	volatile int tracked_key_state_;

	// State of left and right paddle of iambic keyer. The flags
	// are common for keying with keyboard keys and with mouse
	// buttons.
	//
	// A timestamp for libcw needs to be generated only in
	// situations when one of the paddles comes down and the other
	// is up. This is why we observe state of both paddles
	// separately.
	bool is_left_down;
	bool is_right_down;

	// Poll primitives to handle receive errors, characters, and spaces.
	void poll_report_receive_error();
	void poll_receive_character();
	void poll_receive_space();

	// Prevent unwanted operations.
	Receiver (const Receiver &);
	Receiver &operator= (const Receiver &);
};

}  // cw namespace

#endif  // _XCWCP_RECEIVER_H
