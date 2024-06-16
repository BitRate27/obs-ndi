#pragma once
#include <Processing.NDI.Lib.h>
typedef std::vector<std::string> (*ptz_presets_cb)(void *private_data);
void ptz_presets_init(const NDIlib_v4 *);
void ptz_presets_set_source_context_map(std::string source_name, void *context);
void ptz_presets_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv);
void ptz_presets_set_preset_names_cb(ptz_presets_cb);
void ptz_presets_set_source_ndiname_map(std::string source_name,
					std::string ndi_name);
void ptz_presets_add_properties(obs_properties_t *group_ptz);
void ptz_presets_set_defaults(obs_data_t *settings);

