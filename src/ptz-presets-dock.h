#pragma once
#include <Processing.NDI.Lib.h>
void ptz_presets_init();
void ptz_presets_update();
void ptz_presets_add_properties(obs_properties_t *group_ptz);
void ptz_presets_set_defaults(obs_data_t *settings);

