#include "link_status_widget.h"
#include <boost/smart_ptr/detail/spinlock.hpp>

LinkStatusWidget::LinkStatusWidget(QListWidget* parent)
  : QListWidgetItem(parent)
{
  node = nullptr;
}

//void LinkStatusWidget::modify(const std::string& url,
//                              bool scanned, int status_code,
//                              const char* errorMsg)
//{
//  QString text;
//  text.sprintf("url: %s [Task: %s] (GET code: %d)",
//               url.data(), scanned?"pending":"scanned", status_code);
//  if (nullptr != errorMsg)
//    {
//      text.append("Error: ");
//      text.append(errorMsg);
//    }
//  std::lock_guard<boost::detail::spinlock> lk(lockItem);
//  (void)lk;
//  setText(text);
//}


LinkStatusWidget::~LinkStatusWidget()
{
}
