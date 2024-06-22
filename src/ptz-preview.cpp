
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
#include "ptz-controller.h"

struct ptz_preview {
	const NDIlib_v4 *ndiLib;
	NDIlib_recv_instance_t current_recv;
	obs_source_t *current_source;
	std::string ndi_name;
};

static struct ptz_preview *context;

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
	void remove(const K &key) { map_.erase(key); }
	void clear() { map_.clear(); }
	int size() { return (int)map_.size(); };

private:
	std::map<K, V> map_;
};

static MapWrapper<std::string, NDIlib_recv_instance_t> ndi_recv_map;
static MapWrapper<obs_source_t *, std::string> source_ndi_map;

void ptz_preview_set_context(struct ptz_preview *ctx);

void ptz_preview_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv) 
{

	//if (context->ndiLib->recv_ptz_is_supported(recv)) {
		ndi_recv_map.set(ndi_name,recv);
		ptz_preview_set_context(context);
	//}
}
void ptz_preview_set_source_ndiname_map(obs_source_t *source,
				      std::string ndi_name)
{
	source_ndi_map.set(source,ndi_name);
}

void ptz_preview_source_deleted(obs_source_t *source){
	source_ndi_map.remove(source);
	if (context->current_source == source) {
		context->current_source = nullptr;
		context->current_recv = nullptr;
		context->ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
	}
};

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

void ptz_preview_set_context(struct ptz_preview *ctx) 
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
	auto ctx = (struct ptz_preview *)param;
	switch (event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED: {
		ptz_preview_set_context(ctx);
		ptz_presets_update();
		break;
	}
	default:
		break;
	}
}

std::string ptz_preview_get_ndiname() { return context->ndi_name; };
obs_source_t* ptz_preview_get_source() { return context->current_source; };
NDIlib_v4* ptz_preview_get_ndilib() { return (NDIlib_v4*)context->ndiLib; };
NDIlib_recv_instance_t ptz_preview_get_recv() { return context->current_recv; };

void ptz_preview_init(const NDIlib_v4 *ndiLib)
{
	if (context) return;

	context = (ptz_preview *)bzalloc(sizeof(ptz_preview));
	context->ndiLib = ndiLib;
	context->current_recv = nullptr;
	context->current_source = nullptr;
	context->ndi_name = obs_module_text(
		"NDIPlugin.PTZPresetsDock.NotSupported");
	obs_frontend_add_event_callback(ptz_on_scene_changed,
									context);
	ptz_presets_init();
}
