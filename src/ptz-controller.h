#pragma once
#include <Processing.NDI.Lib.h>
#include "../lib/visca27/ViscaAPI.h"

#include <pthread.h>
#include <string>
#include <qwidget.h>
class InteractiveCanvas : public QWidget {
	Q_OBJECT
public:
	explicit InteractiveCanvas(QWidget *parent = nullptr);
public slots:
	void updateImage(int dummy);
protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
private:
	bool drawing = false;
	QImage canvas;
	QPoint lastPoint;
	void drawLineTo(const QPoint &endPoint);
	void resizeEvent(QResizeEvent *event) override;
	void resizeCanvas(const QSize &newSize);
};
void ptz_controller_init();
void ptz_controller_update();