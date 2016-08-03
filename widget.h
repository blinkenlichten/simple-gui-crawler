#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "webgrep/crawler.h"

namespace Ui {
  class Widget;
}

enum class WIDGET_TAB_IDX {
  FOUND_TEXT = 0, PAGE_RENDER, GRAPH_RENDER
};

class Widget : public QWidget
{
  Q_OBJECT

public:
  explicit Widget(QWidget *parent = 0);
  virtual ~Widget();

public slots:
  void onStart();

private:
  Ui::Widget *ui;
  std::shared_ptr<WebGrep::Crawler> crawler;
};

#endif // WIDGET_H
