#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

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
  ~Widget();

private:
  Ui::Widget *ui;
};

#endif // WIDGET_H
