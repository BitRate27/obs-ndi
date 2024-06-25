#pragma once
#include <Processing.NDI.Lib.h>
#include <obs.h>
#include <string>
#include "../lib/visca27/ViscaAPI.h"
void ptz_preview_init(const NDIlib_v4 *);
std::string ptz_preview_get_ndiname();
obs_source_t* ptz_preview_get_source();
ViscaAPI ptz_preview_get_visca_connection();
NDIlib_v4* ptz_preview_get_ndilib();
NDIlib_recv_instance_t ptz_preview_get_recv();
void ptz_preview_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv);
void ptz_preview_set_source_ndiname_map(obs_source_t *source,
					std::string ndi_name);
void ptz_preview_source_deleted(obs_source_t *source);