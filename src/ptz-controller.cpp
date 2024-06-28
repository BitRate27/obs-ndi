#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "ptz-controller.h"
#include "ptz-preview.h"
#include <qevent.h>
#include <qpainter.h>
#include <qlabel.h>
#include <qlayout.h>
#include <chrono>
#include <pthread.h>
#include <thread>
#include <regex>
#include <Processing.NDI.Lib.h>
#include <mutex>
#include <QMetaObject>
#include <QThread>
#include <QObject>

static std::mutex _imageMutex;
static unsigned char *_imageData;
static int _imageWidth;
static int _imageHeight;

// Function to convert a single UYVY pixel pair to two RGB pixels
void UYVYtoRGB(int U, int Y1, int V, int Y2, QRgb &rgb1, QRgb &rgb2)
{
	int C1 = Y1 - 16;
	int C2 = Y2 - 16;
	int D = U - 128;
	int E = V - 128;

	int R1 = (298 * C1 + 409 * E + 128) >> 8;
	int G1 = (298 * C1 - 100 * D - 208 * E + 128) >> 8;
	int B1 = (298 * C1 + 516 * D + 128) >> 8;

	int R2 = (298 * C2 + 409 * E + 128) >> 8;
	int G2 = (298 * C2 - 100 * D - 208 * E + 128) >> 8;
	int B2 = (298 * C2 + 516 * D + 128) >> 8;

	// Clamp the values to 0-255
	rgb1 = qRgb(qBound(0, R1, 255), qBound(0, G1, 255), qBound(0, B1, 255));
	rgb2 = qRgb(qBound(0, R2, 255), qBound(0, G2, 255), qBound(0, B2, 255));
}

// Main conversion function
void convertUYVYtoRGB32(const unsigned char *uyvyData,
					     int width, int height, unsigned char *rgbaData)
{
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 2) {
			int index = (y * width + x) *
				    2; 
			int U = uyvyData[index];
			int Y1 = uyvyData[index + 1];
			int V = uyvyData[index + 2];
			int Y2 = uyvyData[index + 3];

			QRgb rgb1, rgb2;
			UYVYtoRGB(U, Y1, V, Y2, rgb1, rgb2);

			int r1Index = (y * width + x) * 4;
			unsigned int *r1ptr = (unsigned int *)&rgbaData[r1Index];
			*r1ptr = rgb1;

			if (x + 1 < width) { // Check to avoid buffer overflow
				unsigned int *r2ptr =
					(unsigned int *)&rgbaData[r1Index+4];
				*r2ptr = rgb2;
			}
		}
	}
}

InteractiveCanvas::InteractiveCanvas(QWidget *parent)
	: QWidget(parent)
	{
		setAttribute(
			Qt::WA_StaticContents); // Indicates the widget content is static
	}

	void InteractiveCanvas::updateImage(int dummy)
	{
		update();
	}

	void InteractiveCanvas::mousePressEvent(QMouseEvent *event) 
	{
		if (event->button() == Qt::LeftButton) {
			lastPoint = event->pos();
			drawing = true;
		}
	}

	void InteractiveCanvas::mouseMoveEvent(QMouseEvent *event)
	{
		if ((event->buttons() & Qt::LeftButton) && drawing)
			drawLineTo(event->pos());
	}

	void InteractiveCanvas::mouseReleaseEvent(QMouseEvent *event)
	{
		if (event->button() == Qt::LeftButton && drawing) {
			drawLineTo(event->pos());
			drawing = false;
		}
	}

	void InteractiveCanvas::paintEvent(QPaintEvent *event)
	{
		QPainter painter(this);
		{
			std::lock_guard<std::mutex> lock(_imageMutex);
			if (_imageData != nullptr) {
				QImage localCanvas = QImage(
					_imageData, _imageWidth, _imageHeight,
					QImage::Format_RGB32);
				QSize scaledSize = localCanvas.size().scaled(
					this->size(), Qt::KeepAspectRatio);

				// Calculate the top left position to center the image
				QPoint topLeft(
					(this->width() - scaledSize.width()) /
						2,
					(this->height() - scaledSize.height()) /
						2);
				painter.drawImage(QRect(topLeft, scaledSize),
						  localCanvas);
			}
		}
	}

	void InteractiveCanvas::drawLineTo(const QPoint &endPoint)
	{
		QPainter painter(&canvas);
		painter.setPen(QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap,
				    Qt::RoundJoin));
		painter.drawLine(lastPoint, endPoint);

		int rad = (3 / 2) + 2;
		update(QRect(lastPoint, endPoint)
			       .normalized()
			       .adjusted(-rad, -rad, +rad, +rad));
		lastPoint = endPoint;
	}

	void InteractiveCanvas::resizeEvent(QResizeEvent *event)
	{
		if (width() > canvas.width() || height() > canvas.height()) {
			int newWidth = qMax(width() + 128, canvas.width());
			int newHeight = qMax(height() + 128, canvas.height());
			resizeCanvas(QSize(newWidth, newHeight));
			update();
		}
		QWidget::resizeEvent(event);
	}

	void InteractiveCanvas::resizeCanvas(const QSize &newSize)
	{
		if (canvas.size() == newSize)
			return;

		QImage newCanvas(newSize, QImage::Format_RGB32);
		newCanvas.fill(Qt::white);
		QPainter painter(&newCanvas);
		painter.drawImage(QPoint(0, 0), canvas);
		canvas = newCanvas;
	}

class PTZControllerWidget : public QWidget {

public:
	PTZControllerWidget()
	{
		label = new QLabel("");
		label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		QVBoxLayout *vbox = new QVBoxLayout(this);
		vbox->addWidget(label);

		canvas = new InteractiveCanvas(this);
		canvas->setSizePolicy(QSizePolicy::Expanding,
				      QSizePolicy::Expanding);
		vbox->addWidget(canvas);
		canvas->show();

	};
	InteractiveCanvas *canvas;

protected:
	void paintEvent(QPaintEvent *event) override
	{
		if (!event)
			return;

		QPainter painter(this);

		label->setText(ptz_preview_get_ndiname().c_str());
		//obs_source_t *source = ptz_preview_get_source();
		//obs_data_t *settings = obs_source_get_settings(source);
		if (canvas) canvas->update();
		//obs_data_release(settings);
	};

private:
	QLabel *label;
};
typedef struct {

	bool running;
	short x_delta;
	short y_delta;
	short x_start;
	short y_start;
	short x_move;
	short y_move;
	short zoom;
	float pixels_to_pan;
	float pixels_to_tilt;
	visca_tuple_t pt_start;
	bool mouse_down;
	bool drag_start;
	PTZControllerWidget *dialog;
} ptz_controller_t;

static ptz_controller_t *_context;

void ptz_controller_update()
{
	if (_context) _context->dialog->update();
};
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
		visca_error_t errh =
			ptz_preview_get_visca_connection().getHorizontalFlip(
				flip);
		float h_flip = flip ? -1.f : 1.f;
		visca_error_t errv =
			ptz_preview_get_visca_connection().getVerticalFlip(
				flip);
		float v_flip = flip ? -1.f : 1.f;

		visca_error_t errz =
			ptz_preview_get_visca_connection().getZoomLevel(
				s->zoom); 

		s->pixels_to_pan = ptz_pixels_to_pan_ratio(s->zoom);
		s->pixels_to_tilt = ptz_pixels_to_tilt_ratio(s->zoom);

		blog(LOG_INFO,
		     "[obs-ndi] ptz_controller_focus h=%f, v=%f, e=%d, zoom=%d, %f %f",
		     h_flip, v_flip, errz, s->zoom, s->pixels_to_pan,
		     s->pixels_to_tilt);
	}
};

class PTZControllerThread : public QThread {
public:
	PTZControllerThread() {}

protected:
	void run() override
	{	
		auto s = _context;
		blog(LOG_INFO, "[obs-ndi] ptz_controller_thread_run");
		while (s->running) {
			if (ptz_preview_get_recv()) {
				if (s->y_delta != 0) {
					int newZoom = std::clamp(
						s->zoom + (s->y_delta * 4), 0,
						16384);
					blog(LOG_INFO,
					     "[obs-ndi] ptz_controller zooming [%d] %d %d",
					     s->y_delta, s->zoom, newZoom);

					s->zoom = newZoom;
					auto err =
						ptz_preview_get_visca_connection()
							.setZoomLevel(newZoom);

					s->y_delta = 0;
				}
				if (s->drag_start) {
					auto err =
						ptz_preview_get_visca_connection()
							.getPanTilt(
								s->pt_start);
					auto errz =
						ptz_preview_get_visca_connection()
							.getZoomLevel(s->zoom);
					s->pixels_to_pan =
						ptz_pixels_to_pan_ratio(
							s->zoom);
					s->pixels_to_tilt =
						ptz_pixels_to_tilt_ratio(
							s->zoom);
					blog(LOG_INFO,
					     "[obs-ndi] ptz_controller_mouse_click start drag err=%d, errz=%d, xy[%d,%d] pt[%d,%d]",
					     err, errz, s->x_start, s->y_start,
					     s->pt_start.value1,
					     s->pt_start.value2);
					s->drag_start = false;
				}
				if (s->mouse_down) {
					int dx = s->x_start - s->x_move;
					int dy = s->y_start - s->y_move;

					visca_tuple_t dest = {
						s->pt_start.value1 +
							(int)((float)dx *
							      s->pixels_to_pan),
						s->pt_start.value2 +
							(int)((float)dy *
							      s->pixels_to_tilt)};
					auto err =
						ptz_preview_get_visca_connection()
							.setAbsolutePanTilt(
								dest);
					blog(LOG_INFO,
					     "[obs-ndi] ptz_controller_mouse_move xy[%d,%d] pt[%d,%d] px[%6.4f,%6.4f]",
					     s->x_start, s->y_start,
					     dest.value1, dest.value2,
					     s->pixels_to_pan,
					     s->pixels_to_tilt);
				}

				NDIlib_video_frame_v2_t video_frame;
				// Assume 'ndiReceiver' is your NDI receiver instance
				NDIlib_recv_instance_t recv =
					ptz_preview_get_recv();
				if (ptz_preview_get_ndilib()->recv_capture_v2(
					    recv, &video_frame, nullptr,
					    nullptr,
					    1000) == NDIlib_frame_type_video) {
					{
						std::lock_guard<std::mutex> lock(
							_imageMutex);
						if (_imageData == nullptr) {
							_imageData = (unsigned char
									      *)
								bzalloc(video_frame
										.xres *
									video_frame
										.yres *
									4);
							_imageWidth =
								video_frame.xres;
							_imageHeight =
								video_frame.yres;
						}

						convertUYVYtoRGB32(
							video_frame.p_data,
							video_frame.xres,
							video_frame.yres,
							_imageData);
					}
					// Free the frame
					ptz_preview_get_ndilib()
						->recv_free_video_v2(
							recv, &video_frame);

					bool success =
						QMetaObject::invokeMethod(
							s->dialog->canvas,
							"updateImage",
							Qt::QueuedConnection, Q_ARG(int,_imageWidth));
				}
			}
			std::this_thread::sleep_for(
				std::chrono::milliseconds(100));
		};
	}
};

static PTZControllerThread *ptzThread;


void ptz_controller_thread_start(ptz_controller_t *s) {
    s->running = true;
    ptzThread = new PTZControllerThread(); // Assuming ptzThread is added to ptz_controller_t
    ptzThread->start();
    blog(LOG_INFO, "[obs-ndi] ptz_controller_thread_start");
}

void ptz_controller_thread_stop(ptz_controller_t *s) {
    if (s->running) {
        s->running = false;
        ptzThread->quit();
        ptzThread->wait();
        delete ptzThread;
    }
    blog(LOG_INFO, "[obs-ndi] ptz_controller_thread_stop");
    bfree(s);
}

void
ptz_controller_init()
{
    blog(LOG_INFO, "[obs-ndi] obs_module_load: ptz_controller_init");

	_context = (ptz_controller_t *)bzalloc(sizeof(ptz_controller_t));
    _context->dialog = new PTZControllerWidget();
	_imageData = nullptr;
    _context->running = false;

    obs_frontend_add_dock_by_id(
	    obs_module_text("PTZController"),
	    obs_module_text("PTZ Controller"), _context->dialog);

    blog(LOG_INFO, "[obs-ndi] obs_module_load: PTZ Controller Dock added");
    if (!_context->running) {
	    ptz_controller_thread_start(_context);
    }
}