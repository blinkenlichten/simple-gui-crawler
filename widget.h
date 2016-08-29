#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <map>
#include <QTextEdit>
#include <QTreeWidgetItem>
#include <memory>

#include "webgrep/linked_task.h"

class QTimer;

namespace WebGrep {
  class Crawler;
}

namespace Ui {
  class Widget;
}


enum class WIDGET_TAB_IDX {
  URL_LISTS = 0, PAGE_RENDER, TEXT_MATCH_RENDER, GRAPH_RENDER
};

/** Main widget, displays web pages crawling results. */
class Widget : public QWidget
{
  Q_OBJECT

public:
  explicit Widget(QWidget *parent = 0);
  virtual ~Widget();

  typedef std::function<void()> Functor_t;

  //make information string about the node
  static void describe(QString& str, WebGrep::LinkedTask* node);

public slots:
  // check provided GUI arguments and start the crawler
  void onStart();

  // stop/pause the crawler
  void onStop();

  // render + display hint
  void paintEvent(QPaintEvent *event);

  //selecting parsed page displays it's .html content
  void onItemClicked(QTreeWidgetItem *item, int column);

  void onSinglePageScanned(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node);

  //invoked by the parser on each page grep finished
  void onPagesListScanned(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node);

  //sets threads number in runtime:
  void onDialValue(int value);

  void onFunctor(Functor_t);

private slots:
  void onCheckOutTimer();
  void onHelpClicked();

  void updateTreeCaptions();

private:

  void print(WebGrep::LinkedTask* head, void*);
  void updateRenderNodes(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node);


  Ui::Widget *ui;
  std::shared_ptr<QString> bufferedErrorMsg;//< set to display
  bool hasError;
  QTextEdit* webPage, *textDraw;//< primitive HTML renderer, without images etc.

  QString guiTempString;
  QTimer* checkOutTimer;

  std::shared_ptr<WebGrep::Crawler> crawler;//< the crawler

  //stores all downloaded pages and match results in a linked list
  WebGrep::RootNodePtr mainNode;

  struct WidgetConn
  {
    WidgetConn() : node(nullptr), widget(nullptr)
    { }
    WebGrep::RootNodePtr rootTask;
    WebGrep::LinkedTask* node;
    QTreeWidgetItem* widget;
  };
  void makeKnown(WebGrep::LinkedTask* task, QTreeWidgetItem* widget, const WebGrep::RootNodePtr& root);
  void makeRootWidget(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node);

  std::map<WebGrep::LinkedTask*, WidgetConn> taskWidgetsMap;
  std::map<QTreeWidgetItem*, WebGrep::LinkedTask*> widgetsTaskMap;

};

#endif // WIDGET_H
