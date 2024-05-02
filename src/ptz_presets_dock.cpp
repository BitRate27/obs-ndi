#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <qpushbutton.h>
#include "ptz_presets_dock.h"
#include <chrono>
#include <pthread.h>
#include <thread>
#include <Processing.NDI.Lib.h>

/*
* Dock with preset buttons
* - Create dock
* - Register button event handlers
* - Start thread
* 
*/
class PresetButton : QPushButton {
};
	
struct ptz_presets_dock {
	bool enabled;
	bool running;
	const NDIlib_v4 *ndiLib;
	pthread_t ptz_presets_thread;
	NDIlib_recv_instance_t current_recv;
	QWidget *dialog;
	QPushButton *button;
	bool button_pressed;
};
static struct ptz_presets_dock *context;

void ptz_preset_button_pressed()
{
	blog(LOG_INFO, "[obs-ndi] PTZ Preset Button Pressed");
	context->button_pressed = true;
}

void ptz_presets_set_recv(NDIlib_recv_instance_t recv) 
{
	context->current_recv = recv;
	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_recv");
}

void *ptz_presets_thread(void *data)
{
	auto s = (ptz_presets_dock *)data;

	while (s->running) {
		if (s->button_pressed) {
			blog(LOG_INFO, "[obs-ndi] ptz_presets_button_pressed");
			s->ndiLib->recv_ptz_recall_preset(s->current_recv, 1, 5);
			s->button_pressed = false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	};
	return s;
}
void ptz_presets_thread_start(ptz_presets_dock *s)
{
	s->running = true;
	pthread_create(&s->ptz_presets_thread, nullptr, ptz_presets_thread, s);
	blog(LOG_INFO, "[obs-ndi] ptz_presets_thread_start");
}

void ptz_presets_thread_stop(ptz_presets_dock *s)
{
	if (s->running) {
		s->running = false;
		pthread_join(s->ptz_presets_thread, NULL);
	}
}

void ptz_presets_init(const NDIlib_v4 *ndiLib)
{
	if (context)
		return;
	context = (ptz_presets_dock *)bzalloc(sizeof(ptz_presets_dock));
	context->ndiLib = ndiLib;
	context->dialog = new QWidget();
	context->button = new QPushButton("Preset1", context->dialog);
	
	QObject::connect(context->button, &QPushButton::clicked,
			 [=]() { ptz_preset_button_pressed(); });
	context->running = false;
	obs_frontend_add_dock_by_id("Preview PTZ Presets",
				    "Preview PTZ Presets", context->dialog);
	blog(LOG_INFO, "[obs-ndi] obs_module_load: PTZ Presets Dock added");
	if (!context->running) {
		ptz_presets_thread_start(context);
	}
}