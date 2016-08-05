#ifndef LINKSTATUSWIDGET_H
#define LINKSTATUSWIDGET_H

#include <QLabel>
#include <mutex>

class LinkStatusWidget : public QLabel
{
  Q_OBJECT
public:
  explicit LinkStatusWidget(QWidget *parent = 0);
  virtual ~LinkStatusWidget();

  //thread-safe, makes spinlock
  void modify(const std::string& url, bool scanned, int status_code,
              const char* errorMsg = nullptr);

  void paintEvent(QPaintEvent *event);

  void* externalData;
signals:

public slots:

private:
  void* lockItem;
};

#endif // LINKSTATUSWIDGET_H
