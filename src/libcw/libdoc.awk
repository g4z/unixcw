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
# Simple AWK script to produce documentation suitable for processing into
# man pages from a C source file.
# Feed output of this script to libsigs.awk and libfuncs.awk to get
# file with function signatures, and file with function
# signatures+documentation respectively.
#





# Initialize the states, tags, and indexes
BEGIN {
	IDLE = 0
	DOCUMENTATION = 1
	FUNCTION_SPECIFICATION = 2
	FUNCTION_BODY = 3
	state = IDLE

	DOCUMENTATION_TAG = "D"
	FUNCTION_TAG      = "F"
	END_TAG           = "E"

	# line counter, starting from zero for every
	# <function's documentation + function's specification> block
	output_line = 0
}





function handle_global_space()
{
	do {
		# print "GLO" $0 > "/dev/stderr"

		if ($0 ~ /^static /) {
			# potentially a static function declaration
			start = match($0, /[a-zA-Z0-9_\* ]+ \**([a-zA-Z0-9_]+)\(/);
			if (RSTART > 0) {
				len = RLENGTH
				name = substr($0, start, len);
				static_functions[name] = name;
				# print name > "/dev/stderr"
			}
		}
	} while ($0 !~ /^\/\*\*/ && getline)
	# print "GLO LEAVING" > "/dev/stderr"

	# Caught beginning of documentation block (or end of file).
	#
	# Beware, line starting with "/**" may be function's top level
	# comment (documentation block), but it may also be a file's
	# Doxygen top level comment.

	output_line = 0;
}





# Erase documentation lines from output[]
function delete_documentation(line)
{
	while (line >= 0) {
		output[line--] = "";
	}
}





function handle_function_specification()
{
	# catch function's name
	start = match($0, /[a-zA-Z0-9_\* ]+ \**([a-zA-Z0-9_]+)\(/);
	if (RSTART > 0) {
		len = RLENGTH
		name = substr($0, start, len);
		# print name > "/dev/stderr"
	} else {
		# This is not a function specification.
		delete_documentation(output_line - 1)
		output_line = 0

		while ($0 == "" && getline) {
			# read and discard
		}

		return 0
	}


	if (static_functions[name]) {
		# specification of static function;
		# no point in processing it

		delete_documentation(output_line - 1)
		output_line = 0

		while ($0 !~ /\)$/ && getline) {
			# read and discard
		}

		return 0

	} else if (name ~ /_internal\(/) {
		# Internal function, don't allow passing it
		# to documentation of public API.
		delete_documentation(output_line - 1)
		output_line = 0

		while ($0 !~ /\)$/ && getline) {
			# read and discard
		}

		return 0

	} else if (name ~ /test_/) {
		# Internal function, don't allow passing it
		# to documentation of public API.
		delete_documentation(output_line - 1)
		output_line = 0

		while ($0 !~ /\)$/ && getline) {
			# read and discard
		}

		return 0

	} else if (name ~ /main\(/) {
		# Internal function, don't allow passing it
		# to documentation of public API.
		delete_documentation(output_line - 1)
		output_line = 0

		while ($0 !~ /\)$/ && getline) {
			# read and discard
		}

		return 0

	} else {
		# read and save function's specification
		# (possibly multi-line)
		do {
			output[output_line++] = FUNCTION_TAG" "$0
		} while ($0 !~ /\)$/ && getline)

		return 1
	}
}





function handle_function_documentation()
{
	while ($0 !~ /^\*\/$/) {
		# print "DOC" $0 > "/dev/stderr"
		# Some documentation texts still have " * " at the
		# beginning sub (/^ \* /," *")
		sub(/^ \* */,"")

		# Handle Doxygen tags:
		# \brief at the very beginning of top-level function comment,
		# \param in function's parameters specification,
		# \return in function's return values specification.
		sub(/^ *\\brief /, "Brief: ")
		sub(/^ *\\param /, "Parameter: ")
		sub(/^ *\\return /, " Returns: ")

		# Handle my "testedin::" tag
		if ($0 ~ /^ *testedin::/) {
			getline
			continue
		}

		# Handle Doxygen tag:
		# \p in the body of top-level function comment
		start = match($0, /\\p ([0-9a-zA-Z_]+)/);
		if (RSTART > 0) {
			len = RLENGTH
			# 3 - strlen(\\p )
			param_name = substr($0, start + 3, len - 3);
			param_name = "\\fB"param_name"\\fP"
			gsub(/(\\p [0-9a-zA-Z_]+)/, param_name, $0)
			# print param_name > "/dev/stderr"
		}

		output[output_line++] = DOCUMENTATION_TAG" "$0
		getline
	}

	# print "DOC LEAVING" > "/dev/stderr"
}





function handle_function_body()
{
	# Ignore function body lines, but watch for a bracket that
	# closes a function
	while ($0 !~ /^\}/) {
		# read and discard lines of function body
		getline
	}
}





function print_documentation_and_specification()
{
	# Print out the specification and documentation lines we have found;
	# reorder documentation and specification so that documentation
	# lines come after the function signatures.

	for (i = 0; i < output_line; i++) {
		if (index(output[i], DOCUMENTATION_TAG) == 0) {
			print output[i]
		}
	}

	for (i = 0; i < output_line; i++) {
		if (index(output[i], DOCUMENTATION_TAG) != 0) {
			print output[i]
		}
	}

	return i
}





# Ignore all blank lines outside of comments and function bodies
/^[[:space:]]*$/ {
	if (state == IDLE) {
		next
	}
}





# Handle every other line in the file according to the state;
# This is the main 'loop' of the script.
{
	# Process static function declarations and change
	# state on '^/**'
	if (state == IDLE) {
		handle_global_space()
		state = DOCUMENTATION
		next
	}


	# Process function documentation blocks, stopping on ' */'.
	if (state == DOCUMENTATION) {
		handle_function_documentation()
		state = FUNCTION_SPECIFICATION
		next
	}


	# Process function specification line(s), stopping on ')$'.
	if (state == FUNCTION_SPECIFICATION) {
		if (handle_function_specification() == 1) {
			# print "GOING FUN BODY" > "/dev/stderr"
			state = FUNCTION_BODY
			next
		} else {
			# print "GOING IDLE" > "/dev/stderr"
			state = IDLE
		}
	}


	# Process function body, stopping on '^}'
	if (state == FUNCTION_BODY) {
		handle_function_body()
		state = IDLE
	}


	# Print function's documentation and specification,
	# i.e. the data accumulated in above functions
	print_documentation_and_specification()

	print END_TAG

	# prepare for next 'documentation + specification' section
	state = IDLE
	output_line = 0
}





# Simply dump anything we have so far on end of file.
END {
	i = print_documentation_and_specification()
	if (i > 0) {
		print END_TAG
	}
}
