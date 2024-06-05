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
#include <qlineedit.h>

void ptz_preset_button_pressed(int);

class PresetButton : public QPushButton {
public:
	inline PresetButton(QWidget *parent_, int index_)
		: QPushButton(parent_),
		  index(index_)
	{
		this->setText(QString::asprintf("BPreset %d", index));
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
	std::string current_source_name;
	bool scene_changed;
	std::string ndi_name;
	QWidget *dialog;
	QLabel *label;
	PresetButton **buttons;
	int ncols;
	int nrows;
	int button_pressed;
	ptz_presets_cb get_names_cb;
};
static struct ptz_presets_dock *context;
void *lookup_ndicontext(std::string name);

class PTZPresetsWidget : public QWidget {
protected:
	void paintEvent(QPaintEvent *event) override
	{
		QPainter painter(this);

		blog(LOG_INFO, "[obs-ndi] paintEvent ");
		context->label->setText(context->ndi_name.c_str());
		std::vector<std::string> preset_names = {};

		if (context->get_names_cb != nullptr) 
			preset_names = 
				context->get_names_cb(lookup_ndicontext(context->current_source_name));

		for (int b = 0; b < context->nrows * context->ncols; ++b) {
			context->buttons[b]->setEnabled(context->current_recv != nullptr);
			if (b < preset_names.size()) {
				preset_names[b].resize(12);
				context->buttons[b]->setText(preset_names[b].c_str());
			}
		}
	};
};

void ptz_preset_button_pressed(int index)
{
	if ((index >= 0) && (index < context->nrows * context->ncols))
		context->button_pressed = index;
}
void ptz_presets_set_dock_context(struct ptz_presets_dock *ctx);

static std::map<std::string, NDIlib_recv_instance_t> ndi_recv_map;
void ptz_presets_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv) 
{
	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_ndiname_recv_map");
	if (context->ndiLib->recv_ptz_is_supported(recv)) {
		ndi_recv_map[ndi_name] = recv;
		ptz_presets_set_dock_context(context);
	}
}

NDIlib_recv_instance_t lookup_recv(std::string name) {
	auto it = ndi_recv_map.find(name);
	if (it != ndi_recv_map.end()) {
		return it->second;
	}
	return nullptr;
}
static std::map<std::string, std::string> source_ndi_map;
void ptz_presets_set_source_ndiname_map(std::string source_name,
				      std::string ndi_name)
{
	source_ndi_map[source_name] = ndi_name;
}
static std::map<std::string, void *> source_context_map;
void ptz_presets_set_source_context_map(std::string source_name,
					void *context)
{
	source_context_map[source_name] = context;
	blog(LOG_INFO, "[obs-ndi] ptz_presets_set_source_context_map, %s size=%d",
		source_name.c_str(),source_context_map.size());
}
void* lookup_ndicontext(std::string name)
{
	auto it = source_context_map.find(name);
	if (it != source_context_map.end()) {
		return it->second;
	}
	return nullptr;
}
std::string lookup_ndiname(std::string source) {
	auto it = source_ndi_map.find(source);
	if (it != source_ndi_map.end()) {
		return it->second;
	}
	return "";
}
std::string lookup_sourcename(std::string ndiname)
{
	for (const auto &pair : source_ndi_map) {
		if (pair.second == ndiname) {
			return pair.first;
		}
	}
	return "";
}
bool EnumerateSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	
	if (!obs_source_showing(source)) return true; // Only care if it is showing

	const char *name = obs_source_get_name(source);

	std::vector<std::string> *names =
		reinterpret_cast<std::vector<std::string> *>(param);

	auto ndi_name = lookup_ndiname(name);
	auto recv = lookup_recv(ndi_name);
	if (recv != nullptr)
		names->push_back(ndi_name);

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
	
	std::vector<std::string> preview_ndinames;
	CreateListOfNDINames(preview_scene, preview_ndinames);
	
	if (preview_ndinames.size() == 0) {
		ctx->current_recv = nullptr;
		ctx->current_source_name = "";
		ctx->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		return;
	}
	
	obs_source_t *program_source = obs_frontend_get_current_scene();
	auto program_scene = obs_scene_from_source(program_source);
	obs_source_release(program_source);

	std::vector<std::string> program_ndinames;
	CreateListOfNDINames(program_scene, program_ndinames);

	bool found = false;
	for (const std::string &name : preview_ndinames) {
		auto it = std::find(program_ndinames.begin(), program_ndinames.end(), name);
		if (it != program_ndinames.end()) {
			ctx->current_recv = nullptr;
			ctx->current_source_name = "";
			ctx->ndi_name = obs_module_text("NDIPlugin.PTZPresetsDock.OnProgram");
			found = true;
			break;
		}
	}

	if (!found) {				
		auto ndi_name = preview_ndinames[0];
		ctx->current_recv = lookup_recv(ndi_name);

		if (ctx->current_recv != nullptr) {
			blog(LOG_INFO, "[obs-ndi] set source name %s", ndi_name.c_str());
			ctx->ndi_name = ndi_name;		
			ctx->current_source_name = lookup_sourcename(ndi_name);
		}
		else {
			ctx->ndi_name = obs_module_text(
				"NDIPlugin.PTZPresetsDock.NotSupported");
			ctx->current_source_name = "";
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
void ptz_presets_set_preset_names_cb(ptz_presets_cb callback){
	context->get_names_cb = callback;
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
				new PresetButton(context->dialog, ndx);
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