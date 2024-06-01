#pragma once
#include <Processing.NDI.Lib.h>
void ptz_presets_init(const NDIlib_v4 *);
void ptz_presets_set_ndiname_recv_map(std::string ndi_name,
				      NDIlib_recv_instance_t recv);
void ptz_presets_set_source_ndiname_map(std::string source_name,
					std::string ndi_name);