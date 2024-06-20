#pragma once
#include <Processing.NDI.Lib.h>
void ptz_presets_init(const NDIlib_v4 *);
void ptz_presets_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv);
void ptz_presets_set_source_ndiname_map(obs_source_t *source,
					std::string ndi_name);
void ptz_presets_source_deleted(obs_source_t *source);
void ptz_presets_add_properties(obs_properties_t *group_ptz);
void ptz_presets_set_defaults(obs_data_t *settings);

