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

#ifndef _CWMODESET_H
#define _CWMODESET_H

#include <string>
#include <vector>

#include "dictionary.h"
// Forward declaration of opaque dictionary.
//typedef struct dictionary_s dictionary;


//-----------------------------------------------------------------------
//  Class Mode
//-----------------------------------------------------------------------

namespace cw {

// Describes a given operating mode.  All modes have a description, and
// dictionary modes add a way to generate random groups of words from the
// dictionary.

class DictionaryMode;
class KeyboardMode;
class ReceiveMode;

class Mode {
 public:
	Mode (const std::string &description)
		: description_ (description) { }
	virtual ~Mode () { }

	std::string get_description() const;

	bool is_same_type_as(const Mode *other) const;

	virtual const DictionaryMode *is_dictionary() const;
	virtual const KeyboardMode *is_keyboard() const;
	virtual const ReceiveMode *is_receive() const;

 private:
	const std::string description_;  // Mode description

	// Prevent unwanted operations.
	Mode (const Mode &);
	Mode &operator= (const Mode &);
};





class DictionaryMode : public Mode {
 public:
	DictionaryMode (const std::string &description, const cw_dictionary_t *dict)
		: Mode (description), dictionary_ (dict) { }

	std::string get_random_word_group() const;

	virtual const DictionaryMode *is_dictionary() const;

 private:
	const cw_dictionary_t *dictionary_;  // Mode dictionary

	// Prevent unwanted operations.
	DictionaryMode (const DictionaryMode &);
	DictionaryMode &operator= (const DictionaryMode &);
};





class KeyboardMode : public Mode {
 public:
	KeyboardMode (const std::string &description)
		: Mode (description) { }

	virtual const KeyboardMode *is_keyboard() const;

 private:
	// Prevent unwanted operations.
	KeyboardMode (const KeyboardMode &);
	KeyboardMode &operator= (const KeyboardMode &);
};





class ReceiveMode : public Mode {
 public:
	ReceiveMode (const std::string &description)
		: Mode (description) { }

	virtual const ReceiveMode *is_receive() const;

 private:
	// Prevent unwanted operations.
	ReceiveMode (const ReceiveMode &);
	ReceiveMode &operator= (const ReceiveMode &);
};





// Class inline functions.
inline std::string Mode::get_description() const
{
	return description_;
}





inline const DictionaryMode *Mode::is_dictionary() const
{
	return 0;
}





inline const KeyboardMode * Mode::is_keyboard() const
{
	return 0;
}





inline const ReceiveMode *Mode::is_receive() const
{
	return 0;
}





inline const DictionaryMode *DictionaryMode::is_dictionary() const
{
	return this;
}





inline const KeyboardMode *KeyboardMode::is_keyboard() const
{
	return this;
}





inline const ReceiveMode *ReceiveMode::is_receive() const
{
	return this;
}





}  // cw namespace


//-----------------------------------------------------------------------
//  Class ModeSet
//-----------------------------------------------------------------------

namespace cw {

// Aggregates Modes, created from dictionaries and locally, and provides a
// concept of a current mode and convenient access to modes based on the
// current mode setting.

class ModeSet {
 public:
	ModeSet();

	void set_current(int index);
	const Mode *get_current() const;

	int get_count() const;
	const Mode *get(int index) const;

	const DictionaryMode *is_dictionary() const;
	const KeyboardMode *is_keyboard() const;
	const ReceiveMode *is_receive() const;

 private:
	const std::vector<Mode*> *const modes_;
	const Mode *current_;

	// Prevent unwanted operations.
	ModeSet (const ModeSet &);
	ModeSet &operator= (const ModeSet &);
};





// Class inline functions.
inline void ModeSet::set_current (int index)
{
	current_ = modes_->at(index);

	return;
}





inline const Mode *ModeSet::get_current() const
{
	return current_;
}





inline int ModeSet::get_count() const
{
	return modes_->size();
}





inline const Mode *ModeSet::get(int index) const
{
	return modes_->at(index);
}





}  // cw namespace

#endif  // _CWMODESET_H
