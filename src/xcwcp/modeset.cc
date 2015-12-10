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

#include "modeset.h"

#include "dictionary.h"

namespace cw {


//-----------------------------------------------------------------------
//  Class Mode
//-----------------------------------------------------------------------

// is_same_type_as()
//
// Return true if the mode passed in has the same type (dictionary, keyboard,
// or receive) as this mode.
bool Mode::is_same_type_as(const Mode *other) const
{
	return (is_dictionary() && other->is_dictionary())
		|| (is_keyboard() && other->is_keyboard())
		|| (is_receive() && other->is_receive());
}





// get_random_word_group()
//
// Return a string composed of an appropriately sized group of random elements
// from the contained dictionary.
std::string DictionaryMode::get_random_word_group() const
{
	std::string random_group;

	const int group_size = cw_dictionary_get_group_size(dictionary_);
	random_group.resize(group_size);

	for (int group = 0; group < group_size; group++) {
		const char *element = cw_dictionary_get_random_word(dictionary_);
		random_group += element;
	}

	return random_group;
}





//-----------------------------------------------------------------------
//  Class ModeSetHelper
//-----------------------------------------------------------------------

// Collects and aggregates operating modes, constructing from all known
// dictionaries, then adding any local modes.  This is a singleton class,
// constrained to precisely one instance, as a helper for ModeSet.

class ModeSetHelper {
public:
	static const std::vector<Mode*> *get_modes();

private:
	ModeSetHelper();
	~ModeSetHelper();

	std::vector<Mode*> modes_;

	// Prevent unwanted operations.
	ModeSetHelper (const ModeSetHelper &);
	ModeSetHelper &operator= (const ModeSetHelper &);
};


// ModeSetHelper()
//
// Initialize a mode set with dictionary and locally defined modes.
ModeSetHelper::ModeSetHelper()
{
	// Start the modes with the known dictionaries.
	for (const cw_dictionary_t *dict = cw_dictionaries_iterate (NULL);
	     dict;
	     dict = cw_dictionaries_iterate(dict)) {

		const std::string description = cw_dictionary_get_description(dict);
		modes_.push_back(new DictionaryMode(description, dict));
	}

	// Add keyboard send and keyer receive.
	modes_.push_back(new KeyboardMode("Send Keyboard CW"));
	modes_.push_back(new ReceiveMode("Receive Keyed CW"));

	return;
}





// ~ModeSetHelper()
//
// Delete all heap allocated modes in the modes array, and clear the array.
ModeSetHelper::~ModeSetHelper()
{
	for (unsigned int i = 0; i < modes_.size(); i++) {
		delete modes_[i];
	}

	modes_.clear();

	return;
}





// get_modes()
//
// Instantiate the singleton instance of ModeSetHelper and return the Modes
// aggregated in this ModeSetHelper.
const std::vector<Mode*> *ModeSetHelper::get_modes()
{
	static const ModeSetHelper *instance = NULL;

	if (!instance) {
		instance = new ModeSetHelper ();
	}

	return &instance->modes_;
}


//-----------------------------------------------------------------------
//  Class ModeSet
//-----------------------------------------------------------------------

//
// ModeSet()
//
// Set up the modes_ array to contain the singleton-created modes vector,
// and initialize the current mode to the first.
ModeSet::ModeSet()
	: modes_ (ModeSetHelper::get_modes()), current_ (modes_->at(0))
{
}





// is_dictionary()
// is_keyboard()
// is_receive()
//
// Convenience type identification functions for the current mode.
const DictionaryMode *ModeSet::is_dictionary() const
{
	return current_->is_dictionary();
}





const KeyboardMode *ModeSet::is_keyboard() const
{
	return current_->is_keyboard();
}





const ReceiveMode *ModeSet::is_receive() const
{
	return current_->is_receive();
}

}  // cw namespace
