#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "ptz-controller.h"
#include <chrono>
#include <pthread.h>
#include <thread>
#include <regex>
#include <Processing.NDI.Lib.h>

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
void ptz_controller_set_wheel(ptz_controller_t *s, int dx, int dy)
{
	s->x_delta += dx;
	s->y_delta += dy; 
	blog(LOG_INFO, "[obs-ndi] ptz_controller_set_wheel [%d,%d]", s->x_delta,
	     s->y_delta);
}
float ptz_pixels_to_pan_ratio(short zoom)
{
	return (1.0209e-09f * ((float)zoom * (float)zoom)) +
	       (-4.3145e-05f * (float)zoom) +
	       0.4452f;
};
float ptz_pixels_to_tilt_ratio(short zoom)
{
	return (-1.20079e-09f * ((float)zoom * (float)zoom)) +
	       (4.94849e-05f * (float)zoom) + 
		   -5.02822e-01f;
};
void ptz_controller_mouse_click(ptz_controller_t *s, bool mouse_up, int x, int y)
{
	s->mouse_down = !mouse_up;
	if (s->mouse_down) {
		s->x_start = x;
		s->y_start = y;
		s->x_move = x;
		s->y_move = y;
		s->drag_start = true;
	}
}

void ptz_controller_mouse_move(ptz_controller_t *s, int mods, int x,
				int y, bool mouse_leave)
{
	if (s->mouse_down) {
		s->x_move = x;
		s->y_move = y;
	}
}
void ptz_controller_focus(ptz_controller_t *s, bool focus) {
	if (focus) {
		bool flip;
		visca_error_t errh = s->visca.getHorizontalFlip(flip);
		if (errh == VCONNECT_ERR) {
			s->visca.connectCamera(s->ip, 5678);
			errh = s->visca.getHorizontalFlip(flip);
		}
		float h_flip = flip ? -1.f : 1.f;
		visca_error_t errv = s->visca.getVerticalFlip(flip);
		float v_flip = flip ? -1.f : 1.f;

		visca_error_t errz = s->visca.getZoomLevel(s->zoom); 

		s->pixels_to_pan = ptz_pixels_to_pan_ratio(s->zoom);
		s->pixels_to_tilt = ptz_pixels_to_tilt_ratio(s->zoom);

		blog(LOG_INFO,
		     "[obs-ndi] ptz_controller_focus h=%f, v=%f, e=%d, zoom=%d, %f %f",
		     h_flip, v_flip, errz, s->zoom, s->pixels_to_pan,
		     s->pixels_to_tilt);
	}
};
void *ptz_controller_thread(void *data)
{
    auto s = (ptz_controller_t *)data;

    while (s->running) {
	    if (s->ndi_recv) {
		    if (s->y_delta != 0) {
			    int newZoom = std::clamp(s->zoom + (s->y_delta * 4),
						     0, 16384);
			    blog(LOG_INFO,
				 "[obs-ndi] ptz_controller zooming [%d] %d %d",
				 s->y_delta, s->zoom, newZoom);

			    s->zoom = newZoom;			    
				auto err = s->visca.setZoomLevel(newZoom);

			    s->y_delta = 0;
		    }
		    if (s->drag_start) {
			    auto err =
				    s->visca.getPanTilt(s->pt_start);
			    auto errz = s->visca.getZoomLevel(s->zoom);
			    s->pixels_to_pan = ptz_pixels_to_pan_ratio(s->zoom);
			    s->pixels_to_tilt =
				    ptz_pixels_to_tilt_ratio(s->zoom);
			    blog(LOG_INFO,
				 "[obs-ndi] ptz_controller_mouse_click start drag err=%d, errz=%d, xy[%d,%d] pt[%d,%d]",
				 err, errz, s->x_start, s->y_start, s->pt_start.value1,
				 s->pt_start.value2);
			    s->drag_start = false;
		    }
		    if (s->mouse_down) {
			    int dx = s->x_start - s->x_move;
			    int dy = s->y_start - s->y_move;

			    visca_tuple_t dest = {
				    s->pt_start.value1 +
					    (int)((float)dx * s->pixels_to_pan),
				    s->pt_start.value2 +
					    (int)((float)dy *
						  s->pixels_to_tilt)};
			    auto err =
				    s->visca.setAbsolutePanTilt(dest);
			    blog(LOG_INFO,
				 "[obs-ndi] ptz_controller_mouse_move xy[%d,%d] pt[%d,%d] px[%6.4f,%6.4f]",
				 s->x_start, s->y_start, dest.value1,
				 dest.value2, s->pixels_to_pan, s->pixels_to_tilt);
		    }
	    }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };
    return s;
}

void ptz_controller_thread_start(ptz_controller_t *s)
{
	s->running = true;
	pthread_create(&s->ptz_thread, nullptr, ptz_controller_thread, s);
	blog(LOG_INFO, "[obs-ndi] ptz_controller_thread_start");
}

void ptz_controller_set_recv(ptz_controller_t *s, NDIlib_recv_instance_t recv)
{
	s->ndi_recv = nullptr;
	if (recv) { 		
		s->ndi_recv = recv;
		if (s->ndiLib->recv_ptz_is_supported(recv)) {
			const char *p_url =
				s->ndiLib->recv_get_web_control(recv);
			blog(LOG_INFO,
			     "[obs-ndi] ptz_controller_set_recv url=%s", p_url);
			sprintf_s(s->ip,100,"%s",extractIPAddress(std::string(p_url)).c_str());		
			s->ndiLib->recv_free_string(recv, p_url);
			
		} else {
			sprintf_s(s->ip,100,"127.0.0.1");
		}
		s->visca.connectCamera(std::string(s->ip),5678);
		blog(LOG_INFO, "[obs-ndi] ptz_controller_set_recv ip=%s",s->ip);

		if (!s->running) ptz_controller_thread_start(s);
	}
}

void ptz_controller_thread_stop(ptz_controller_t *s)
{
    if (s->running) {
        s->running = false;
        pthread_join(s->ptz_thread, NULL);
    }
    bfree(s);
}

ptz_controller_t *
ptz_controller_init(const NDIlib_v4 *ndiLib, obs_source_t *obs_source)
{
    auto context = (ptz_controller_t *)bzalloc(sizeof(ptz_controller_t));
    context->ndiLib = ndiLib;
    context->obs_source = obs_source;
    blog(LOG_INFO, "[obs-ndi] obs_module_load: ptz_controller_init");
    context->running = false;
    return context;
}