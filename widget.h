#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <map>
#include <QTextEdit>
#include <QTreeWidgetItem>
#include <memory>

class QTimer;

namespace WebGrep {
  class Crawler;
  class LinkedTask;
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


  //invoked by the parser on each page grep finished
  void onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode,
                     WebGrep::LinkedTask* node);

  //sets threads number in runtime:
  void onDialValue(int value);

  void onFunctor(Functor_t);

private slots:
  void onCheckOutTimer();
  void onHelpClicked();

private:

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

  struct WidgetConn
  {
    WidgetConn() : node(nullptr), widget(nullptr)
    { }
    std::shared_ptr<WebGrep::LinkedTask> rootTask;
    WebGrep::LinkedTask* node;
    QTreeWidgetItem* widget;
  };
  void makeKnown(WebGrep::LinkedTask* task, QTreeWidgetItem* widget, const std::shared_ptr<WebGrep::LinkedTask>& root);

  std::map<WebGrep::LinkedTask*, WidgetConn> taskWidgetsMap;
  std::map<QTreeWidgetItem*, WebGrep::LinkedTask*> widgetsTaskMap;

};

#endif // WIDGET_H
