
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
	pthread_t ptz_presets_thread;
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
		if (!event) return;

		QPainter painter(this);

		context->label->setText(ptz_preview_get_ndiname().c_str());
		obs_source_t* source = ptz_preview_get_source();
		obs_data_t *settings = obs_source_get_settings(source);

		for (int b = 0; b < PROP_NPRESETS; ++b) {
			if (ptz_preview_get_recv() != nullptr) {
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
void ptz_presets_update() {
	if (context) context->dialog->update();
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
	if ((ptz_preview_get_recv() != nullptr) &&
		(index > 0) && 
		(index <= context->nrows * context->ncols))
		context->button_pressed = index;
}
void ptz_presets_hotkey_function(void* priv, obs_hotkey_id id, obs_hotkey_t* hotkey, bool pressed)
{
	(void)id;
	(void)hotkey;
	(void)pressed;

	if (pressed) {
		PresetButton *button = static_cast<PresetButton*>(priv);
        ptz_preset_button_pressed(button->index);
    }
}

void *ptz_presets_thread(void *data)
{
	auto s = (ptz_presets_dock *)data;

	while (s->running) {
		if (ptz_preview_get_recv() && s->button_pressed >= 0) {
			ptz_preview_get_ndilib()->recv_ptz_recall_preset(ptz_preview_get_recv(),
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
void ptz_presets_add_properties(obs_properties_t *group_ptz)
{
	for (int pp = 1; pp <= PROP_NPRESETS; pp++) {
		auto p = obs_properties_add_text(
			group_ptz, QString(PROP_PRESET).arg(pp).toUtf8(),
			QString("Preset %1").arg(pp).toUtf8(),
			OBS_TEXT_DEFAULT);

		obs_property_set_modified_callback2(
			p,
			(obs_property_modified2_t)ptz_presets_property_modified,
			(void *)context->buttons[pp - 1]);
	}
};

void ptz_presets_init()
{
	if (context) return;

	context = (ptz_presets_dock *)bzalloc(sizeof(ptz_presets_dock));
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
	}
}
