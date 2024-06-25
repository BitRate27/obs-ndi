
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
#include "ptz-preview.h"
#include "ptz-presets-dock.h"
#include "ptz-controller.h"
#include <regex>

struct recv_info_t {
	NDIlib_recv_instance_t recv;
	std::string ndi_name;
	ViscaAPI visca;
	char ip[100];
	int port;
};


typedef struct {
	obs_source_t *source;
	NDIlib_recv_instance_t recv;
	std::string ndi_name;
	ViscaAPI visca;
} ptz_preview; // current preview context

static ptz_preview _current;
static const NDIlib_v4 *_ndiLib;

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
	V getOrCreate(const K &key) const { return map_[key]; };

private:
	std::map<K, V> map_;
};

static MapWrapper<std::string, recv_info_t> ndi_recv_map;
static MapWrapper<obs_source_t *, std::string> source_ndi_map;
void ptz_preview_set_current();

std::string extractIPAddress(const std::string &str)
{
	std::regex ipRegex(
		"(\\b(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\."
		"(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\b)");
	std::smatch match;
	if (std::regex_search(str, match, ipRegex)) {
		return match.str();
	}
	return "";
}
void ptz_preview_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv) {
	//if (_ndiLib->recv_ptz_is_supported(recv)) 
	{

		recv_info_t recv_info = ndi_recv_map.get(ndi_name);

		recv_info.recv = recv;
		const char *p_url = _ndiLib->recv_get_web_control(recv);
		if (p_url) {
			blog(LOG_INFO,
			     "[obs-ndi] ptz_controller_set_recv url=%s", p_url);
			sprintf_s(recv_info.ip, 100, "%s",
				  extractIPAddress(std::string(p_url)).c_str());
			_ndiLib->recv_free_string(recv, p_url);
		} else {
			sprintf_s(recv_info.ip, 100, "127.0.0.1");
		}
		recv_info.visca.connectCamera(std::string(recv_info.ip),
					      5678);
		
		ndi_recv_map.set(ndi_name, recv_info);

		blog(LOG_INFO, "[obs-ndi] ptz_controller_set_recv ip=%s",
		     recv_info.ip);
	}
}
void ptz_preview_set_source_ndiname_map(obs_source_t *source,
					std::string ndi_name)
{
	source_ndi_map.set(source, ndi_name);
}

void ptz_preview_source_deleted(obs_source_t *source){

	if (_current.source == source) {
		_current.source = nullptr;
		_current.ndi_name = "";

		_current.visca.disconnectCamera();
	}
	source_ndi_map.remove(source);
};

bool EnumerateSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (!scene) return false;
	obs_source_t *source = obs_sceneitem_get_source(item);
	
	if (!obs_source_showing(source)) return true; // Only care if it is showing

	std::vector<std::string> *names =
		reinterpret_cast<std::vector<std::string> *>(param);

	auto ndi_name = source_ndi_map.get(source);
	auto recv_info = ndi_recv_map.get(ndi_name);
	if (recv_info.recv != nullptr)
		names->push_back(ndi_name);

	return true;
}

void CreateListOfNDINames(obs_scene_t *scene,
			     std::vector<std::string> &names)
{
	obs_scene_enum_items(scene, EnumerateSceneItems, &names);
}

void ptz_preview_set_current() 
{
	obs_source_t *preview_source =
		obs_frontend_get_current_preview_scene();

	auto preview_scene = obs_scene_from_source(preview_source);
	obs_source_release(preview_source);
	
	std::vector<std::string> preview_ndinames;
	CreateListOfNDINames(preview_scene, preview_ndinames);
	
	if ((preview_source != nullptr) && (preview_ndinames.size() == 0)) {
		_current.recv = nullptr;
		_current.source = nullptr;
		_current.ndi_name = obs_module_text(
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
				_current.recv = nullptr;
				_current.source = nullptr;
				_current.ndi_name = obs_module_text(
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
	_current.recv = ndi_recv_map.get(ndi_name).recv;

	if (_current.recv != nullptr) {
		_current.ndi_name = ndi_name;		
		_current.source = source_ndi_map.reverseLookup(ndi_name);
		recv_info_t recv_info = ndi_recv_map.get(ndi_name);
		_current.visca = recv_info.visca;
	}
	else {
		_current.ndi_name = obs_module_text(
			"NDIPlugin.PTZPresetsDock.NotSupported");
		_current.source = nullptr;
	}
}

void ptz_on_scene_changed(enum obs_frontend_event event, void *param)
{
	blog(LOG_INFO, "[obs-ndi] ptz_on_scene_changed(%d)", event);
	auto ctx = (struct ptz_preview *)param;
	switch (event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED: {
		ptz_preview_set_current();
		ptz_presets_update();
		ptz_controller_update();
		break;
	}
	default:
		break;
	}
}

std::string ptz_preview_get_ndiname()
{
	return _current.ndi_name;
};
obs_source_t *ptz_preview_get_source()
{ 
	return _current.source;
};
NDIlib_v4* ptz_preview_get_ndilib() 
{ 
	return (NDIlib_v4*)_ndiLib; 
};
NDIlib_recv_instance_t ptz_preview_get_recv()
{
	return _current.recv;
};
ViscaAPI ptz_preview_get_visca_connection() 
{
	return _current.visca;
};

void ptz_preview_init(const NDIlib_v4 *ndiLib)
{
	_ndiLib = ndiLib;
	_current.source = nullptr;
	_current.recv = nullptr;
	_current.visca = ViscaAPI();
	_current.ndi_name = "";
	obs_frontend_add_event_callback(ptz_on_scene_changed, &_current);
	ptz_presets_init();
	ptz_controller_init();
}
