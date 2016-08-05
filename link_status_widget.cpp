#include "link_status_widget.h"
#include <boost/smart_ptr/detail/spinlock.hpp>

LinkStatusWidget::LinkStatusWidget(QWidget *parent) : QLabel(parent)
{
  externalData = nullptr;
  lockItem = new boost::detail::spinlock;
}

void LinkStatusWidget::modify(const std::string& url,
                              bool scanned, int status_code,
                              const char* errorMsg)
{
  QString text;
  text.sprintf("url: %s [Task: %s] (GET code: %d)",
               url.data(), scanned?"pending":"scanned", status_code);
  if (nullptr != errorMsg)
    {
      text.append("Error: ");
      text.append(errorMsg);
    }
  std::lock_guard<boost::detail::spinlock> lk(*((boost::detail::spinlock*)lockItem));
  (void)lk;
  setText(text);
}

void LinkStatusWidget::paintEvent(QPaintEvent *event)
{
  std::lock_guard<boost::detail::spinlock> lk(*((boost::detail::spinlock*)lockItem));
  (void)lk;
  QLabel::paintEvent(event);
}

LinkStatusWidget::~LinkStatusWidget()
{
  delete (boost::detail::spinlock*)lockItem;
}
