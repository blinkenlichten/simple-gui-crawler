#ifndef LINKSTATUSWIDGET_H
#define LINKSTATUSWIDGET_H

#include <QListWidgetItem>
#include <QListWidget>
#include <mutex>
#include <memory>
#include "boost/smart_ptr/detail/spinlock.hpp"

namespace WebGrep {
  class LinkedTask;
}

class LinkStatusWidget : public QListWidgetItem
{
public:
  explicit LinkStatusWidget(QListWidget *parent = 0);
  virtual ~LinkStatusWidget();

  std::shared_ptr<WebGrep::LinkedTask> rootTask;
  WebGrep::LinkedTask* node;


signals:

public slots:

private:
};

#endif // LINKSTATUSWIDGET_H
