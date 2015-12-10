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


#ifndef _XCWCP_DISPLAY_H
#define _XCWCP_DISPLAY_H

#include <QString>


//-----------------------------------------------------------------------
//  Class Display
//-----------------------------------------------------------------------

// Describes an extremely simply text display interface.  The interface is
// minimized and abstracted like this to make changes to the underlying
// implementation easy.

namespace cw {

class Application;
class DisplayImpl;

class Display {
 public:
	Display (Application *application, QWidget *parent);
	QWidget *get_widget() const;

	// Minimal text display interface; add a character, remove a character,
	// and clear the display completely.
	void append(char c);
	void backspace();
	void clear();

	// Minimal pass-through status bar interface.
	void show_status(const QString &status);
	void clear_status();

 private:
	Application *application_;
	DisplayImpl *implementation_;

	// Prevent unwanted operations.
	Display (const Display &);
	Display &operator= (const Display &);
};

}  // cw namespace

#endif  // _XCWCP_DISPLAY_H
