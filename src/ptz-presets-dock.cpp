#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <qpushbutton.h>
#include <qgridlayout.h>
#include <QPainter>
#include <qlabel.h>
#include "ptz-presets-dock.h"
#include <chrono>
#include <thread>
#include <pthread.h>

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
	inline void PresetButtonClicked() { ptz_preset_button_pressed(index); }
	int index;
};

struct ptz_presets_dock {
	bool running;
	const NDIlib_v4 *ndiLib;
	pthread_t ptz_presets_thread;
	NDIlib_recv_instance_t current_recv;
	obs_source_t *current_source;
	std::string ndi_name;
	QWidget *dialog;
	QLabel *label;
	PresetButton **buttons;
	int ncols;
	int nrows;
	int button_pressed;
};
static struct ptz_presets_dock *context;

class PTZPresetsWidget : public QWidget {
protected:
	void paintEvent(QPaintEvent *event) override
	{
		QPainter painter(this);
		context->label->setText(context->ndi_name.c_str());
		blog(LOG_INFO, "[ptz] paintEvent");
	};
};

void ptz_preset_button_pressed(int index)
{
	if ((index >= 0) && (index < context->nrows * context->ncols))
		context->button_pressed = index;
}

void ptz_on_scene_changed(enum obs_frontend_event event, void *param)
{
	blog(LOG_INFO, "[obs-ndi] ptz_on_scene_changed(%d)", event);
	auto ctx = (struct ptz_presets_dock *)param;
	switch (event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
		if (context->current_source)
			obs_source_release(context->current_source);
		obs_source_t *temp_preview_source =
			obs_frontend_get_current_preview_scene();
		obs_source_t *temp_program_source =
			obs_frontend_get_current_scene();
		context->current_source = nullptr;
		if (temp_preview_source != temp_program_source)
			context->current_source = temp_preview_source;
		else
			obs_source_release(temp_preview_source);
		obs_source_release(temp_program_source);	
		break;
	}

	default:
		break;
	}
}

void ptz_presets_set_recv(obs_source_t *source, NDIlib_recv_instance_t recv,
			  const char *ndiname)
{
	if (source != context->current_source)
		return;

	if (recv && context->ndiLib->recv_ptz_is_supported(recv)) {
		obs_source_release(context->current_source);
		context->current_source = nullptr;
		context->current_recv = nullptr;
		context->ndi_name = "Source does not support PTZ";
		context->dialog->update();
		blog(LOG_INFO, "[obs-ndi] ptz_presets_set_recv [%s]", context->ndi_name);
		return;
	}

	obs_source_release(context->current_source);
	context->current_source = nullptr;	
	context->current_recv = recv;
	context->ndi_name = ndiname;
	context->dialog->update();
	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_recv [%s]", context->ndi_name);
	return;
}

void *ptz_presets_thread(void *data)
{
	auto s = (ptz_presets_dock *)data;

	while (s->running) {
		if (s->current_recv && s->button_pressed >= 0) {
			s->ndiLib->recv_ptz_recall_preset(s->current_recv,
							  s->button_pressed, 5);
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
	if (context) return;

	context = (ptz_presets_dock *)bzalloc(sizeof(ptz_presets_dock));
	context->ndiLib = ndiLib;
	context->dialog = new PTZPresetsWidget();

	QVBoxLayout *vbox = new QVBoxLayout(context->dialog);

	context->label = new QLabel("Preview source not PTZ capable");
	context->label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	vbox->addWidget(context->label);

	QGridLayout *grid = new QGridLayout();
	vbox->addLayout(grid);

	context->nrows = 3;
	context->ncols = 3;
	context->buttons = (PresetButton **)bzalloc(
		sizeof(PresetButton *) * context->nrows * context->ncols);

	for (int i = 0; i < context->nrows; ++i) {
		for (int j = 0; j < context->ncols; ++j) {
			int ndx = i * context->ncols + j;
			context->buttons[ndx] =
				new PresetButton(context->dialog, ndx + 1);
			context->buttons[ndx]->setEnabled(true);
			grid->addWidget(context->buttons[ndx], i, j);
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
		obs_frontend_add_event_callback(ptz_on_scene_changed,
						context);
	}
}