#include "link_status_widget.h"
#include <boost/smart_ptr/detail/spinlock.hpp>

LinkStatusWidget::LinkStatusWidget(QListWidget* parent)
  : QListWidgetItem(parent)
{
  node = nullptr;
}

LinkStatusWidget::~LinkStatusWidget()
{
}
