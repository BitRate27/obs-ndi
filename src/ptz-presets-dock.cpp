
#include <chrono>
#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <pthread.h>
#include <qgridlayout.h>
#include <qlabel.h>
#include <QMouseEvent>
#include <qpainter.h>
#include <qpushbutton.h>
#include <thread>
#include <qlineedit.h>
#include "ptz-presets-dock.h"

#define PROP_PRESET "preset%1"
#define PROP_NPRESETS 9

#define MAX_PRESET_NAME_LENGTH 12

void ptz_preset_button_pressed(int);

class PresetButton : public QPushButton {
public:
    inline PresetButton(QWidget *parent_, int index_)
        : QPushButton(parent_),
          index(index_)
    {
        this->setText("");
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

template<typename K, typename V> class MapWrapper {
public:
	void set(const K &key, const V &value) { map_[key] = value; }

	V get(const K &key) const
	{
		auto it = map_.find(key);
		if (it != map_.end()) {
			return it->second;
		}
		return V();
	}

	K reverseLookup(const V &value) const
	{
		for (const auto &pair : map_) {
			if (pair.second == value) {
				return pair.first;
			}
		}
		return K();
	}
	void clear() { map_.clear(); }
	int size() { return (int)map_.size(); };

private:
	std::map<K, V> map_;
};

static MapWrapper<std::string, NDIlib_recv_instance_t> ndi_recv_map;
static MapWrapper<obs_source_t *, std::string> source_ndi_map;

class PTZPresetsWidget : public QWidget {
protected:
	void paintEvent(QPaintEvent *event) override
	{		
		if (!event) return;

		QPainter painter(this);

		context->label->setText(context->ndi_name.c_str());
		obs_source_t* source = context->current_source;
		obs_data_t *settings = obs_source_get_settings(source);

		for (int b = 0; b < PROP_NPRESETS; ++b) {
			if (context->current_recv != nullptr) {
				context->buttons[b]->setEnabled(true);
				context->buttons[b]->setText(
					obs_data_get_string(
						settings,
						QString(PROP_PRESET)
							.arg(b+1)
							.toUtf8()));
			} else {
				context->buttons[b]->setEnabled(false);
				context->buttons[b]->setText("");
			}
		}

		obs_data_release(settings);
	};
};

bool ptz_presets_property_modified(void *priv, obs_properties_t *props,
		       obs_property_t *property, obs_data_t *settings )
{
	bool changed = false;
	PresetButton *button = static_cast<PresetButton*>(priv);
    const char* property_name = obs_property_name(property);
    const char* value = obs_data_get_string(settings, property_name);
    if (strlen(value) > MAX_PRESET_NAME_LENGTH) {
	    std::string temp = value;
	    obs_data_set_string(settings, obs_property_name(property),
				temp.substr(0, MAX_PRESET_NAME_LENGTH).c_str());
	    button->setText(temp.c_str());
	    return true;
    }

	button->setText(value);

    return false;
}
void ptz_preset_button_pressed(int index)
{
	if ((context->current_recv != nullptr) &&
		(index > 0) && 
		(index <= context->nrows * context->ncols))
		context->button_pressed = index;
}
void ptz_presets_hotkey_function(void* priv, obs_hotkey_id id, obs_hotkey_t* hotkey, bool pressed)
{
    if (pressed) {
		PresetButton *button = static_cast<PresetButton*>(priv);
        ptz_preset_button_pressed(button->index);
    }
}

void ptz_presets_set_dock_context(struct ptz_presets_dock *ctx);

void ptz_presets_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv) 
{

	//if (context->ndiLib->recv_ptz_is_supported(recv)) {
		ndi_recv_map.set(ndi_name,recv);
		ptz_presets_set_dock_context(context);
	//}
}
void ptz_presets_set_source_ndiname_map(obs_source_t *source,
				      std::string ndi_name)
{
	source_ndi_map.set(source,ndi_name);
}

bool EnumerateSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (!scene) return false;
	obs_source_t *source = obs_sceneitem_get_source(item);
	
	if (!obs_source_showing(source)) return true; // Only care if it is showing

	std::vector<std::string> *names =
		reinterpret_cast<std::vector<std::string> *>(param);

	auto ndi_name = source_ndi_map.get(source);
	auto recv = ndi_recv_map.get(ndi_name);
	if (recv != nullptr)
		names->push_back(ndi_name);

	return true;
}

void CreateListOfNDINames(obs_scene_t *scene,
			     std::vector<std::string> &names)
{
	obs_scene_enum_items(scene, EnumerateSceneItems, &names);
}

void ptz_presets_add_properties(obs_properties_t *group_ptz){
	for (int pp = 1; pp <= PROP_NPRESETS; pp++) {
		auto p = obs_properties_add_text(
			group_ptz, QString(PROP_PRESET).arg(pp).toUtf8(),
			QString("Preset %1").arg(pp).toUtf8(),
			OBS_TEXT_DEFAULT);

		obs_property_set_modified_callback2(
			p, 
			(obs_property_modified2_t)ptz_presets_property_modified, 
			(void*)context->buttons[pp-1]);
	}
};
void ptz_presets_set_dock_context(struct ptz_presets_dock *ctx) 
{
	obs_source_t *preview_source =
		obs_frontend_get_current_preview_scene();

	auto preview_scene = obs_scene_from_source(preview_source);
	obs_source_release(preview_source);
	
	std::vector<std::string> preview_ndinames;
	CreateListOfNDINames(preview_scene, preview_ndinames);
	
	if ((preview_source != nullptr) && (preview_ndinames.size() == 0)) {
		ctx->current_recv = nullptr;
		ctx->current_source = nullptr;
		ctx->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		return;
	}
	
	obs_source_t *program_source = obs_frontend_get_current_scene();
	auto program_scene = obs_scene_from_source(program_source);
	obs_source_release(program_source);

	std::vector<std::string> program_ndinames;
	CreateListOfNDINames(program_scene, program_ndinames);

	if (preview_source != nullptr) {
		// Check if preview source also on program
		for (const std::string &name : preview_ndinames) {
			auto it = std::find(program_ndinames.begin(),
					    program_ndinames.end(), name);
			if (it != program_ndinames.end()) {
				ctx->current_recv = nullptr;
				ctx->current_source = nullptr;
				ctx->ndi_name = obs_module_text(
					"NDIPlugin.PTZPresetsDock.OnProgram");
				return;
			}
		}
	}
	
	// If there are preview ndi sources with ptz support, then use the first one, 
	// otherwise we are not in Studio mode, so allow preset recall on program
	// source if has ptz support.
	std::string ndi_name = (preview_ndinames.size() > 0) ? preview_ndinames[0] : 
		(program_ndinames.size() > 0) ? program_ndinames[0] : "";
	ctx->current_recv = ndi_recv_map.get(ndi_name);

	if (ctx->current_recv != nullptr) {
		ctx->ndi_name = ndi_name;		
		ctx->current_source = source_ndi_map.reverseLookup(ndi_name);
	}
	else {
		ctx->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		ctx->current_source = nullptr;
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
			blog(LOG_INFO,
			     "[obs-ndi] ptz_presets_button_pressed [%d]",
			     s->button_pressed);
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

void ptz_presets_set_defaults(obs_data_t *settings)
{
	for (int pp = 1; pp <= PROP_NPRESETS; pp++) {
		obs_data_set_default_string(
			settings, QString(PROP_PRESET).arg(pp).toUtf8(),
			QString("Preset %1").arg(pp).toUtf8());
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
			obs_hotkey_id hotkeyId = 
				obs_hotkey_register_frontend(QString("PTZPreset%1").arg(ndx+1).toUtf8(),
											 QString("PTZ Preset %1").arg(ndx+1).toUtf8(), 
											 ptz_presets_hotkey_function, 
											 (void*)context->buttons[ndx]);
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
