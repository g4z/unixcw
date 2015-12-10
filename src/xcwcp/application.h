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

#ifndef _XCWCP_APPLICATION_H
#define _XCWCP_APPLICATION_H

#include <QMainWindow>
#include <QToolButton>
#include <QComboBox>
#include <QSpinBox>

#include <string>
#include <deque>

#include "modeset.h"
#include "cw_common.h"


//-----------------------------------------------------------------------
//  Class Application
//-----------------------------------------------------------------------

// Encapsulates the outermost Xcwcp Qt application.  Defines slots and
// signals, as well as the usual class information.

namespace cw {

class Display;
class Sender;
class Receiver;

class Application : public QMainWindow
{
	Q_OBJECT
 public:
	Application();

	// Handle key press and mouse button press events
	void key_event(QKeyEvent *event);
	void mouse_event(QMouseEvent *event);
	void check_audio_system(cw_config_t *config);
 protected:
	void closeEvent (QCloseEvent *event);

	private slots:
		// Qt widget library callback functions.
		void about();
		void startstop();
		void start();
		void stop();
		void new_instance();
		void clear();
		void sync_speed();
		void speed_change();
		void frequency_change();
		void volume_change();
		void gap_change();
		void mode_change();
		void curtis_mode_b_change();
		void adaptive_receive_change();
		void fonts();
		void colors();
		void toggle_toolbar(void);
		void poll_timer_event();

 private:
	// Class variable to enable sharing of the libcw across instances.  Set to
	// the 'this' of the CW user instance, or NULL if no current user.
	static Application *libcw_user_application_instance;

	QPixmap xcwcp_icon;

	bool play_;
	QPixmap start_icon;
	QPixmap stop_icon;
	// GUI elements used throughout the class.
	QToolBar *toolbar; // main toolbar

	QToolButton *startstop_button_;
	QAction *startstop_; // Shared between toolbar and Progam menu
	QComboBox *mode_combo_;
	QSpinBox *speed_spin_;
	QSpinBox *frequency_spin_;
	QSpinBox *volume_spin_;
	QSpinBox *gap_spin_;


	QMenu *program_menu_;
	QAction *new_window_;
	QAction *clear_display_;
	QAction *sync_speed_;
	QAction *close_;
	QAction *quit_;

	QMenu *settings_;
	QAction *reverse_paddles_;
	QAction *curtis_mode_b_;
	QAction *adaptive_receive_;
	QAction *font_settings_;
	QAction *color_settings_;
	QAction *toolbar_visibility_;

	QMenu *help_;
	QAction *about_;

	Display *display_;

	int file_synchronize_speed_id_;
	int file_start_id_;
	int file_stop_id_;

	// Set of modes used by the application; initialized from dictionaries, with
	// keyboard and receive modes added.
	ModeSet modeset_;

	// Sender and receiver.
	Sender *sender_;
	Receiver *receiver_;

	// Poll timer, used to ensure that all of the application processing can
	// be handled in the foreground, rather than in the signal handling context
	// of a libcw tone queue low callback.
	QTimer *poll_timer_;

	// Flag indicating if this instance is currently using the libcw. Of
	// course xcwcp is an application that links to libcw, but this flag
	// is for *active* use of libcw, i.e when "play"/"start" button in
	// xcwcp's UI has been pressed.
	bool is_using_libcw_;

	// Saved receive speed, used to reinstate adaptive tracked speed on start.
	int saved_receive_speed_;

	// Keying callback function for libcw.  There is a static version for
	// the whole class, and an instance version for each object.  The class
	// version calls the relevant instance version, based on which instance is
	// the current registered libcw user.
	static void libcw_keying_event_static (void *, int key_state);
	void libcw_keying_event (int key_state);

	// Wrappers for creating UI.
	void make_central_widget(void);
	void make_toolbar(void);
	void make_mode_combo(void);
	void make_program_menu(void);
	void make_settings_menu(void);
	void make_help_menu(void);

	void make_auxiliaries_begin(void);
	void make_auxiliaries_end(void);


	// Prevent unwanted operations.
	Application (const Application &);
	Application &operator= (const Application &);
};

}  // cw namespace

#endif  // _XCWCP_APPLICATION_H
