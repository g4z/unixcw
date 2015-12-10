.\"
.\" UnixCW CW Tutor Package - LIBCW
.\" Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
.\" Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
.\"
.\" This program is free software; you can redistribute it and/or
.\" modify it under the terms of the GNU General Public License
.\" as published by the Free Software Foundation; either version 2
.\" of the License, or (at your option) any later version.
.\"
.\" This program is distributed in the hope that it will be useful,
.\" but WITHOUT ANY WARRANTY; without even the implied warranty of
.\" MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
.\" GNU General Public License for more details.
.\"
.\" You should have received a copy of the GNU General Public License along
.\" with this program; if not, write to the Free Software Foundation, Inc.,
.\" 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
.\"
.\"
.TH LIBCW 3 "CW Tutor Package" "libcw ver. 6.4.1" \" -*- nroff -*-
.SH NAME
.\"
libcw \- general purpose Morse code functions library
.\"
.\"
.\"
.SH SYNOPSIS
.\"
.nf
.B #include <libcw.h>
.sp
.fi
include(signatures)
.PP
.\"
.\"
.\"
.SS DESCRIPTION
.\"
.B libcw
is a general purpose CW (Morse code) functions library.  It contains
routines for converting characters into Morse code representations
and back again, for sending Morse code characters, and for receiving
characters.  It also contains routines to emulate an Iambic Morse
keyer, and a straight key.
.PP
The library can be included in any program that wishes to make use of
these features.  It forms the heart of three Morse code tutor
applications that accompany the package in which it is distributed.
.PP
See the \fBcw\fP(7) man page for information on Morse code timings,
and the dot and dash representations for the various Morse characters.
.\"
.\"
.\"
.SS TONE QUEUE
.\"
.B libcw
contains an inbuilt tone queue.  The queue is emptied by background
processing, using SIGALRM calls and itimers, so a caller program can
continue with other tasks while the library sends tones and keys any
external device.
.PP
As well as being used by the library functions that sound Morse code
characters and provide a keyer sidetone, the primitive tone queue
functions are publicly available to caller programs.
.PP
.\"
.\"
.\"
.SS CONTROLLING AN EXTERNAL DEVICE
.\"
.B libcw
may be passed the address of a function that controls external keying.
This function is called each time the library changes the keying state,
either as a result of sending a Morse character or representation, or
as a result of an iambic keyer or straight key state change.  The argument
passed is a single integer, TRUE for key-down, and FALSE for key-up.
.PP
.B libcw
calls the external keying function only when the keying state changes.
A call is likely each time a tone is taken off the tone queue.
.PP
.\"
.\"
.\"
.SS SENDING CW CHARACTERS AND STRINGS
.\"
.B libcw
offers several functions that send individual characters and character
strings as Morse code.  It also offers functions that allow
specialized 'representations' to be sent.  A 'representation' is an ASCII
string that consists of only the characters '.' and '-'.
.PP
Characters and strings are converted into representations, and then the
correct tones for the dots and dashes in these representations are queued
on the tone queue, for action by the background queue emptying process.
.PP
.\"
.\"
.\"
.SS RECEIVING CW CHARACTERS AND REPRESENTATIONS
.\"
.B libcw
contains functions to allow it to receive Morse code.  To receive, the
library must be told when a tone start is detected, and when a tone end
is detected.  It then determines whether the tone was a dot or a dash
depending on the timing difference between the two.  After the required
silence gap has passed, the library may be queried to see what the
received representation or character was.
.PP
Errors in receiving may be detected by means of the flags passed back on
receive character functions.
.PP
.\"
.\"
.\"
.SS IAMBIC KEYER
.\"
.B libcw
offers functions to simulate an Iambic Morse keyer.  The caller program
needs to tell the library of paddle state changes.  Iambic keyer functions
are mutually exclusive with character send and straight key functions.
.PP
.\"
.\"
.\"
.SS STRAIGHT KEY
.\"
.B libcw
offers simple functions to allow effective pass-through of straight key
information.  The caller program needs to tell the library of key state
changes.  Straight key functions are mutually exclusive with character
send and iambic keyer functions.
.PP
.\"
.\"
.\"
.SS RETURN CODES
.\"
Some of the library's function return a return code of type int.
The return code has two values, as defined in libcw.h: \fBCW_SUCCESS\fP
or \fBCW_FAILURE\fP. The two symbolic constants are guaranteed to be
identical to boolean \fBtrue\fP and \fBfalse\fP.
.PP
.\"
.\"
.\"
.SS FUNCTIONS
The following list describes the functions available to a \fBlibcw\fP caller:
include(functions)
.PP
.\"
.\"
.\"
.SH NOTES
.\"
Despite the fact that this manual page constantly and consistently
refers to Morse code elements as dots and dashes, DO NOT think in these
terms when trying to learn Morse code.  Always think of them as 'dit's
and 'dah's.
.PP
.B libcw
uses system itimers for its internal timing.  On most UNIX flavours,
itimers are not guaranteed to signal a program exactly at the specified
time, and they generally offer a resolution only as good as the normal
system 'clock tick' resolution.  An itimer SIGALRM usually falls on a
system clock tick, making it accurate to no better than 10mS on a typical
100Hz kernel.
.PP
The effect of this is that an itimer period is generally either
exactly as specified, or, more likely, slightly longer.  At higher
WPM settings, the cumulative effect of this affects timing accuracy,
because at higher speeds, there are fewer 10mS clock ticks in a dot
period.  For example, at 12 WPM, the dot length is 100mS, enough to
contain five kernel clock ticks; at 60 WPM, the dot length is 20mS,
or just two kernel clock ticks.  So at higher speeds, the effect of itimer
resolutions becomes more pronounced.
.PP
.\"
.\"
.\"
.SH SEE ALSO
.\"
Man pages for \fBcw\fP(7,LOCAL), \fBcw\fP(1,LOCAL), \fBcwgen\fP(1,LOCAL),
\fBcwcp\fP(1,LOCAL), and \fBxcwcp\fP(1,LOCAL).
.\"
