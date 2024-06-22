#pragma once
#include <Processing.NDI.Lib.h>
#include "../lib/visca27/ViscaAPI.h"
#include <pthread.h>
#include <string>
#include <qwidget.h>
typedef struct {
	obs_source_t *obs_source;
	NDIlib_recv_instance_t ndi_recv;
	pthread_t ptz_thread;
	const NDIlib_v4 *ndiLib;
	bool running;
	short x_delta;
	short y_delta;
	short x_start;
	short y_start;
	short x_move;
	short y_move;
	short zoom;
	float pixels_to_pan;
	float pixels_to_tilt;
	visca_tuple_t pt_start;
	bool mouse_down;
	bool drag_start;
	char ip[100];
	ViscaAPI visca;
	QWidget *dialog;
} ptz_controller_t;
ptz_controller_t *ptz_controller_init(const NDIlib_v4 *ndiLib,
				      obs_source_t *obs_source);
void ptz_controller_update();
void ptz_controller_set_recv(ptz_controller_t *,NDIlib_recv_instance_t);
void ptz_controller_set_wheel(ptz_controller_t *, int, int);
void ptz_controller_mouse_click(ptz_controller_t *s, bool mouse_up, int x, int y);
void ptz_controller_mouse_move(ptz_controller_t *s, int mods, int x, int y,
			       bool mouse_leave);
void ptz_controller_focus(ptz_controller_t *s, bool focus);
void ptz_controller_thread_stop(ptz_controller_t *);