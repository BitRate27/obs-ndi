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
	};
};

void ptz_preset_button_pressed(int index)
{
	if ((index >= 0) && (index < context->nrows * context->ncols))
		context->button_pressed = index;
}

static std::map<std::string, NDIlib_recv_instance_t> ndi_recv_map;
void ptz_presets_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv) 
{
	if (ndiLib->recv_ptz_is_supported(recv)) {
		ndi_recv_map[ndi_name] = recv;
		ptz_presets_set_dock_context(context);
	}
}

static std::map<std::string, std::string> source_ndi_map;
void ptz_presets_set_source_ndiname_map(std::string source_name,
				      std::string ndi_name)
{
	source_ndi_map[source_name] = ndi_name;
}

bool EnumerateSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	
	if (!obs_source_showing(source)) return true; // Only care if it is showing

	const char *name = obs_source_get_name(source);

	std::vector<std::string> *names =
		reinterpret_cast<std::vector<std::string> *>(param);

	auto it = source_ndi_map.find(name);
	if (it != source_ndi_map.end()) {
		// Only add if ptz supported: ndi_recv_map only 
		// has ptz supported ndi receivers
		auto recv_it = ndi_recv_map.find(it->second);
		if (recv_it != ndi_recv_map.end()) {
			names->push_back(it->second);
		}
	}

	return true;
}

void CreateListOfNDINames(obs_scene_t *scene,
			     std::vector<std::string> &names)
{
	obs_scene_enum_items(scene, EnumerateSceneItems, &names);
}

void ptz_presets_set_dock_context(struct ptz_presets_dock *ctx) 
{
	obs_source_t *preview_source =
		obs_frontend_get_current_preview_scene();
	auto preview_scene = obs_scene_from_source(preview_source);
	obs_source_release(preview_source);
	
	std::vector<std::string> preview_sources;
	CreateListOfNDINames(preview_scene, preview_sources);
	
	if (preview_sources.size() == 0) {
		ctx->current_recv = nullptr;
		ctx->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		break;
	}

	obs_source_t *program_source = obs_frontend_get_current_scene();
	auto program_scene = obs_scene_from_source(program_source);
	obs_source_release(program_source);

	std::vector<std::string> program_sources;
	CreateListOfNDINames(program_scene, program_sources);

	bool found = false;
	for (const std::string &name : preview_sources) {
		auto it = std::find(program_sources.begin(), program_sources.end(), name);
		if (it != program_sources.end()) {
			ctx->current_recv = nullptr;
			ctx->ndi_name = obs_module_text("NDIPlugin.PTZPresetsDock.OnProgram");
			found = true;
			break;
		}
	}

	if (!found) {				
		auto it = ndi_recv_map.find(name);
		if (it != ndi_recv_map.end()) {
			ctx->current_recv = it->second;
			ctx->ndi_name = name;
		}
		else {
			ctx->current_recv = nullptr;
			ctx->ndi_name = obs_module_text(
				"NDIPlugin.PTZPresetsDock.NotSupported");
		}
	}
}

void ptz_on_scene_changed(enum obs_frontend_event event, void *param)
{
	blog(LOG_INFO, "[obs-ndi] ptz_on_scene_changed(%d)", event);
	auto ctx = (struct ptz_presets_dock *)param;
	switch (event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED: {
		ptz_presets_set_dock_context(ctx);
		ctx->dialog->update();
		break;
	}
	default:
		break;
	}
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