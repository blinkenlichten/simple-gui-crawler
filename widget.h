#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QListWidgetItem>
#include <memory>
#include "boost/smart_ptr/detail/spinlock.hpp"

class QTimer;

namespace WebGrep {
  class Crawler;
  class LinkedTask;
}

namespace Ui {
  class Widget;
}


enum class WIDGET_TAB_IDX {
  FOUND_TEXT = 0, PAGE_RENDER, TEXT_RENDER, GRAPH_RENDER
};

/** Main widget, displays web pages crawling results. */
class Widget : public QWidget
{
  Q_OBJECT

public:
  explicit Widget(QWidget *parent = 0);
  virtual ~Widget();

  typedef std::function<void()> Functor_t;

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

  void onFunctor(Functor_t);

private slots:
  void onCheckOutTimer();

private:
  void populateListsFunction(WebGrep::LinkedTask* head, void*);

  void print(WebGrep::LinkedTask* head, void*);

  Ui::Widget *ui;
  std::shared_ptr<WebGrep::Crawler> crawler;//< the crawler
  std::shared_ptr<QString> bufferedErrorMsg;//< set to display
  bool hasError;
  QTextEdit* webPage, *textDraw;//< primitive HTML renderer, without images etc.

  QString guiTempString;
  QTimer* checkOutTimer;

  //stores all downloaded pages and match results in a linked list
  std::shared_ptr<WebGrep::LinkedTask> mainNode;

};

#endif // WIDGET_H
