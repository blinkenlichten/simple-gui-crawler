#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QListWidgetItem>
#include <memory>
#include "boost/smart_ptr/detail/spinlock.hpp"

namespace WebGrep {
  class Crawler;
  class LinkedTask;
}

namespace Ui {
  class Widget;
}

class GraphWidget;

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
  void paintEvent(QPaintEvent *event);
  void onList2Clicked(QListWidgetItem* item);

  void onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode,
                     WebGrep::LinkedTask* node);

public:
  Ui::Widget *ui;
  std::shared_ptr<WebGrep::Crawler> crawler;
  std::shared_ptr<QString> bufferedErrorMsg;
  bool hasError;
  QTextEdit* webPage;
  QString guiTempString;

  //stores all downloaded pages and match results in a linked list
  std::shared_ptr<WebGrep::LinkedTask> mainNode;

//  GraphWidget* graphView;
};

#endif // WIDGET_H
