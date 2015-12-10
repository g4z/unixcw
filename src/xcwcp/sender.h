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

#ifndef _XCWCP_SENDER_H
#define _XCWCP_SENDER_H

#include <QKeyEvent>

#include <string>
#include <deque>




//-----------------------------------------------------------------------
//  Class Sender
//-----------------------------------------------------------------------

// Encapsulates the main application sender data and functions.  Sender
// abstracts the send character queue, polling, and event handling.

namespace cw {

class Display;
class Mode;

class Sender {
 public:
	Sender (Display *display)
		: display_ (display), is_queue_idle_ (true) { }

	// Poll timeout handler, and keypress event handler.
	void poll(const Mode *current_mode);
	void handle_key_event(QKeyEvent *event, const Mode *current_mode);

	// Clear out queued data on stop, mode change, etc.
	void clear();

 private:
	// Display used for output.
	Display *display_;

	// Deque and queue manipulation functions, used to handle and
	// maintain the buffer of characters awaiting libcw send.
	bool is_queue_idle_;
	std::deque<char> send_queue_;

	void dequeue_character();
	void enqueue_string(const std::string &word);
	void delete_character();

	// Prevent unwanted operations.
	Sender(const Sender &);
	Sender &operator= (const Sender &);
};

}  // cw namespace

#endif  // _XCWCP_SENDER_H
