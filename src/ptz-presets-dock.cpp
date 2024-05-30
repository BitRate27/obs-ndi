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
	bool scene_changed;
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
		for (int b = 0; b < context->nrows * context->ncols; ++b) {
			context->buttons[b]->setEnabled(context->current_recv);
		}
		blog(LOG_INFO, "[ptz] paintEvent %s %d",
		     context->ndi_name.c_str(), (context->current_recv != nullptr));
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
		context->scene_changed = true;
		context->current_recv = nullptr;
		context->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		context->dialog->update();
		break;
	default:
		break;
	}
}

void ptz_presets_set_recv(obs_source_t *source, NDIlib_recv_instance_t recv,
			  const char *ndiname)
{
	if (!context->scene_changed)
		return;

	auto source_name = obs_source_get_name(source);

	obs_source_t *preview_source =
		obs_frontend_get_current_preview_scene();	
	auto preview_scene = obs_scene_from_source(preview_source);
	obs_source_release(preview_source);

	// Find the source in the current preview scene
	obs_sceneitem_t *found_scene_item =
		obs_scene_find_source_recursive(preview_scene, source_name);

	if (found_scene_item == nullptr)
		return;

	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_recv source [%s] on preview",
	     source_name);

	if (recv && !context->ndiLib->recv_ptz_is_supported(recv)) {
		context->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		context->dialog->update();
		blog(LOG_INFO,
		     "[obs-ndi] ptz_presets_set_recv not supported [%s]",
		     context->ndi_name.c_str());
		return;
	}

	context->scene_changed = false;

	// Find the source in the current program scene
	obs_source_t *program_source = obs_frontend_get_current_scene();
	auto program_scene = obs_scene_from_source(program_source);
	obs_source_release(program_source);
	found_scene_item =
		obs_scene_find_source_recursive(program_scene, source_name);

	if (found_scene_item != nullptr) {
		context->ndi_name =
			obs_module_text("NDIPlugin.PTZPresetsDock.OnProgram");
		context->dialog->update();
		blog(LOG_INFO,
		     "[obs-ndi] ptz_presets_set_recv source [%s] on program",
		     source_name);
		return;
	}

	context->current_recv = recv;
	context->ndi_name = ndiname;
	context->dialog->update();
	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_recv finished [%s]",
	     context->ndi_name.c_str());
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

	context->label = new QLabel("");
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

	obs_frontend_add_dock_by_id(obs_module_text("NDIPlugin.PTZPresetsDock.Title"),
		obs_module_text("NDIPlugin.PTZPresetsDock.Title"),
		context->dialog);

	blog(LOG_INFO, "[obs-ndi] obs_module_load: PTZ Presets Dock added");
	if (!context->running) {
		ptz_presets_thread_start(context);
		obs_frontend_add_event_callback(ptz_on_scene_changed,
						context);
	}
}