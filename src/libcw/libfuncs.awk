#!/bin/awk -f
#
# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
#
# AWK script to produce function documentation strings from processed
# C source. Pass output of libdoc.awk script to input of this script.
#





# Initialize states and tags
BEGIN {
	IDLE = 0
	TYPE = 1
	SPECIFICATION = 2
	DOCUMENTATION = 3
	state = IDLE

	FUNCTION_TAG      = "F"
	DOCUMENTATION_TAG = "D"
	END_TAG           = "E"

	# For handling a series of Doxygen's "\li" tags
	in_list = 0
}





# Handle each line containing function specification
$1 == FUNCTION_TAG {
	sub(FUNCTION_TAG, "")
	sub(/^ */, "")
	gsub(/\t/, "       ")

	if (state != SPECIFICATION) {
		# first line of specification;
		# print empty line before printing (possibly
		# multi-line) specification
		print("\n.sp\n");
	}

	printf(".br\n.B \"%s\"\n", $0)

	if ($0 ~ /\)$/) {
		# newline line after last line of (possibly multi-line)
		# specification
		# printf(".sp\n", $0)
	}
	state = SPECIFICATION
	next
}





# Handle all documentation lines
$1 == DOCUMENTATION_TAG {
	if (state == SPECIFICATION) {
		state = DOCUMENTATION
		# line break between function prototype and
		# function documentation
		print(".br");
	}

	sub(DOCUMENTATION_TAG, "")
	sub(/^ +/, "")

	# line break for printing consecutive 'Returns: ' and 'Parameter: '
	# in separate lines
	if ($0 ~ /^Returns:/ || $0 ~ /^Parameter:/) {
		print(".br")
	}

	# "\li" list item from Doxygen documentation.
	if ($0 ~ /^\\li /) {
		if (in_list == 0) {
			in_list = 1
			# Opening indentation mark for a list.
			print(".RS")
		}
		print(".IP \\[bu]")
		print(substr($0, 4))

	} else {
		print $0
	}

	if ($0 == "") {
		# It is assumed that last item on a list is followed
		# with empty line.
		if (in_list == 1) {
			in_list = 0
			# Closing indentation mark for a list.
			print(".RE")
		}
	}

	next
}





# On end of a function specification, reset state
$1 == END_TAG {
	state = IDLE
	next
}
