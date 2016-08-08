#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QListWidgetItem>
#include <memory>

namespace WebGrep {
  class Crawler;
  class LinkedTask;
}

namespace Ui {
  class Widget;
}


enum class WIDGET_TAB_IDX {
  FOUND_TEXT = 0, PAGE_RENDER, GRAPH_RENDER
};

/** Main widget, displays web pages crawling results. */
class Widget : public QWidget
{
  Q_OBJECT

public:
  explicit Widget(QWidget *parent = 0);
  virtual ~Widget();

public slots:
  // check provided GUI arguments and start the crawler
  void onStart();

  // render + display hint
  void paintEvent(QPaintEvent *event);

  //selecting parsed page displays it's .html content
  void onList2Clicked(QListWidgetItem* item);

  //invoked by the parser on each page grep finished
  void onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode,
                     WebGrep::LinkedTask* node);

private:
  Ui::Widget *ui;
  std::shared_ptr<WebGrep::Crawler> crawler;//< the crawler
  std::shared_ptr<QString> bufferedErrorMsg;//< set to display
  bool hasError;
  QTextEdit* webPage;//< primitive HTML renderer, without images etc.

  QString guiTempString;

  //stores all downloaded pages and match results in a linked list
  std::shared_ptr<WebGrep::LinkedTask> mainNode;

//  GraphWidget* graphView;
};

#endif // WIDGET_H
