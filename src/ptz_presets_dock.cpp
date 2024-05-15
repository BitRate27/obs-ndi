#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <qpushbutton.h>
#include <qgridlayout.h>
#include <qlabel.h>
#include "ptz_presets_dock.h"
#include <chrono>
#include <pthread.h>
#include <thread>
#include <Processing.NDI.Lib.h>

void ptz_preset_button_pressed(int);

class PresetButton : public QPushButton {
public:
	inline PresetButton(QWidget *parent_, int index_)
		: QPushButton(parent_),
		  index(index_)
	{
		this->setText(QString::asprintf("Preset %d", index));
		this->setSizePolicy(QSizePolicy::Expanding,
				    QSizePolicy::Expanding);
		QObject::connect(this, &QPushButton::clicked, this,
				 &PresetButton::PresetButtonClicked);
	}
	inline void PresetButtonClicked()
	{ 
		ptz_preset_button_pressed(index);
	}
	int index;
};
	
struct ptz_presets_dock {
	bool enabled;
	bool running;
	const NDIlib_v4 *ndiLib;
	pthread_t ptz_presets_thread;
	NDIlib_recv_instance_t current_recv;
	QWidget *dialog;
	QLabel *label;
	PresetButton **buttons;
	int button_pressed;
};
static struct ptz_presets_dock *context;

void ptz_preset_button_pressed(int index)
{
	blog(LOG_INFO, "[obs-ndi] PTZ Preset Button Pressed (%d)",index);
	context->button_pressed = index;
}

void ptz_presets_set_recv(NDIlib_recv_instance_t recv, const char *ndiname) 
{
	context->current_recv = recv;
	context->label->setText(ndiname);
	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_recv [%s]",ndiname);
}

void *ptz_presets_thread(void *data)
{
	auto s = (ptz_presets_dock *)data;

	while (s->running) {
		if (s->current_recv && s->button_pressed >= 0) {
			blog(LOG_INFO,
			     "[obs-ndi] ptz_presets_button_pressed (%d)",
			     s->button_pressed);
			s->ndiLib->recv_ptz_recall_preset(s->current_recv, s->button_pressed, 5);
			s->button_pressed = -1;
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
;	if (context)
		return;

	context = (ptz_presets_dock *)bzalloc(sizeof(ptz_presets_dock));
	context->ndiLib = ndiLib;
	context->dialog = new QWidget();

	QVBoxLayout *vbox = new QVBoxLayout(context->dialog);

	context->label = new QLabel("No PTZ NDI source previewed");
	context->label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	vbox->addWidget(context->label);

	QGridLayout *grid = new QGridLayout();
	vbox->addLayout(grid);

	context->buttons = (PresetButton **)bzalloc(sizeof(PresetButton *) * 9);

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			int ndx = i * 3 + j;
			context->buttons[ndx] = new PresetButton(
				context->dialog, ndx + 1);
			grid->addWidget(context->buttons[ndx], i,
					      j);
		}
	}
	context->dialog->setLayout(grid);
	
	context->button_pressed = -1;
	context->running = false;
	obs_frontend_add_dock_by_id("Preview PTZ Presets",
				    "Preview PTZ Presets", context->dialog);
	
	blog(LOG_INFO, "[obs-ndi] obs_module_load: PTZ Presets Dock added");
	if (!context->running) {
		ptz_presets_thread_start(context);
	}
}