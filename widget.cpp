#include "widget.h"
#include "ui_widget.h"

#include <mutex>
#include <QTimer>
#include <QMetaObject>
#include <functional>
#include <QTextEdit>
#include <QTextCodec>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include "webgrep/crawler.h"
#include "webgrep/linked_task.h"
#include <cassert>

//-----------------------------------------------------------------------------
Widget::Widget(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Widget)
{
  QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
  qRegisterMetaType<Functor_t>("Functor_t");

  ui->setupUi(this);
  setLayout(ui->topLayout);

  ui->groupBoxText->setLayout(ui->horizontalLayoutText);
  QTabWidget* tabw = ui->tabWidget;
  QWidget* listsWidget = tabw->widget((int)WIDGET_TAB_IDX::URL_LISTS);
  listsWidget->setLayout(ui->listVerticalLayout_2);

  QWidget* pageWidget = tabw->widget((int)WIDGET_TAB_IDX::PAGE_RENDER);
  pageWidget->setLayout(ui->pageHorizontalLayout);

  QWidget* graphWidget = tabw->widget((int)WIDGET_TAB_IDX::GRAPH_RENDER);
  graphWidget->setLayout(ui->graphHorizontalLayout);
  ui->groupBoxText->setMaximumHeight(60);

  webPage = new QTextEdit(pageWidget);
  ui->pageHorizontalLayout->addWidget(webPage);

  auto textWidget = tabw->widget((int)WIDGET_TAB_IDX::TEXT_MATCH_RENDER);
  textWidget->setLayout(ui->verticalLayoutTextMatches);
  textDraw = new QTextEdit(textWidget);
  textDraw->setAcceptRichText(false);
  ui->verticalLayoutTextMatches->addWidget(textDraw);

  tabw->setCurrentIndex(0);

  guiTempString.reserve(256);

  crawler = std::make_shared<WebGrep::Crawler>();

  connect(ui->dial, &QDial::valueChanged, this, &Widget::onDialValue);

  connect(ui->buttonStart, &QPushButton::clicked,
          this, &Widget::onStart, Qt::DirectConnection);

  connect(ui->buttonStop, &QPushButton::clicked,
          this, &Widget::onStop, Qt::DirectConnection);


  //check for tasks status sometimes
  checkOutTimer = new QTimer(this);
  connect(checkOutTimer, &QTimer::timeout, this, &Widget::onCheckOutTimer, Qt::DirectConnection);
  checkOutTimer->start(2000);

  connect(ui->helpButton, &QPushButton::clicked, this, &Widget::onHelpClicked);

  //on click in second list -> show downloaded .html web page
  connect(ui->treeWidget, &QTreeWidget::itemClicked,
          this, &Widget::onItemClicked, Qt::QueuedConnection);

  //will pass the exception Error tot he GUI:
  crawler->setExceptionCB
      ( [this] (const std::string& what)
  {
      bufferedErrorMsg = std::make_shared<QString>(QString::fromStdString(what));
  } );


  crawler->setPageScannedCB([this](WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node)
  {
    this->onSinglePageScanned(rootNode, node);
  }
  );

  /** On new linked page scan finished.*/
  crawler->setLevelSpawnedCB([this]
  (WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node)
  {
    this->onPagesListScanned(rootNode, node);
  } );

}
//-----------------------------------------------------------------------------
Widget::~Widget()
{
  delete ui;
  delete webPage;
}
//-----------------------------------------------------------------------------
#include <stdio.h>

void Widget::print(WebGrep::LinkedTask* head, void*)
{
  std::array<char, 256> buf; buf.fill(0);
  ::snprintf(buf.data(), buf.size(),
            "==== item_ptr: %p url: %s {child: %p next: %p}",
           head, head->grepVars.targetUrl.data(),
           WebGrep::ItemLoadRelaxed(head->child),
            WebGrep::ItemLoadRelaxed(head->next));
  std::cerr << buf.data() << "\n";
}

//-----------------------------------------------------------------------------
void Widget::onItemClicked(QTreeWidgetItem *item, int column)
{
  (void)column;
  auto iter = widgetsTaskMap.find(item);
  if (iter == widgetsTaskMap.end())
    { return; }
  WebGrep::LinkedTask* node = (*iter).second;

  //render scanned web-page
  const WebGrep::GrepVars& g(node->grepVars);
  QString content = QString::fromStdString(g.pageContent);
  webPage->setHtml(content);
  ui->tabWidget->setCurrentIndex((int)WIDGET_TAB_IDX::PAGE_RENDER);
  textDraw->setAcceptRichText(false);
  textDraw->setPlainText(content);

  std::string::const_iterator begin = g.pageContent.begin();
  QTextCursor cs = textDraw->textCursor();

  QTextCharFormat fmt;
  for(const WebGrep::GrepVars::CIteratorPair& iter : g.matchTextVector)
    {
      cs.setPosition(iter.first - begin);
      cs.select(QTextCursor::LineUnderCursor);
      fmt.setBackground(QBrush(Qt::cyan));
      fmt.setTableCellColumnSpan((int)(iter.second - iter.first));
      cs.setCharFormat(fmt);
    }
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//must work only in GUI thread:
void Widget::onCheckOutTimer()
{
//  onPagesListScanned(mainNode, mainNode.get());
}
//-----------------------------------------------------------------------------
void Widget::describe(QString& str, WebGrep::LinkedTask* node)
{
  const WebGrep::GrepVars& g(node->grepVars);
  if (!g.pageIsParsed)
    {
      str.sprintf("url: %s (GET code: %d) (Status: %s )",
                            g.targetUrl.data(), g.responseCode,
                            g.pageIsReady? "downloaded" : "pending in the queue");
    }
  else
    {
      str.sprintf("url: %s (GET code: %d) (Status:parsed) (Text Matches: %u) (URL matches: %u)",
                            g.targetUrl.data(), g.responseCode,
                            g.matchTextVector.size(), g.matchURLVector.size());

    }
}
//-----------------------------------------------------------------------------
void Widget::makeKnown(WebGrep::LinkedTask *task, QTreeWidgetItem *widget, const WebGrep::RootNodePtr& root)
{
  WidgetConn cn;
  cn.node = task;
  cn.widget = widget;
  cn.rootTask = root;
  taskWidgetsMap[task] = cn;
  widgetsTaskMap[widget] = task;
}

void Widget::makeRootWidget(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node)
{
  taskWidgetsMap.clear();
  widgetsTaskMap.clear();

  for(QTreeWidgetItem* item = ui->treeWidget->takeTopLevelItem(0);
      item != nullptr; item = ui->treeWidget->takeTopLevelItem(0))
    {
      delete item;
    }

  mainNode = rootNode;
  //create root widget:
  auto rootItem = new QTreeWidgetItem(ui->treeWidget);
  makeKnown(node, rootItem, this->mainNode);
}
//-----------------------------------------------------------------------------
void Widget::onSinglePageScanned(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node)
{
  auto ftor = std::function<void()>
  ([this, rootNode, node]()
  {
      auto iter = taskWidgetsMap.find(node);
      if(taskWidgetsMap.end() == iter && 0 == node->level)
        {
          makeRootWidget(rootNode, node);
          iter = taskWidgetsMap.begin();
        }

      QTreeWidgetItem* w = iter->second.widget;
      Widget::describe(this->guiTempString, node);
      w->setText(0, this->guiTempString);
  });
  QMetaObject::invokeMethod(this, "onFunctor", Qt::QueuedConnection, Q_ARG(Functor_t, ftor) );
}
//-----------------------------------------------------------------------------
void Widget::updateRenderNodes(WebGrep::RootNodePtr rootNode, WebGrep::LinkedTask* node)
{
  float r = (float)rootNode->linksCounterPtr->load();
  r /= (float)rootNode->maxLinksCountPtr->load();
  r *= 100;


  ui->progressBar->setValue(r);
  ui->treeWidget->setColumnCount(1);
  //find the Level1 branch:
  WebGrep::LinkedTask* parent = WebGrep::ItemLoadAcquire(node->parent);
  auto iter = taskWidgetsMap.find(nullptr == parent? node : parent);

  if(this->mainNode != rootNode || taskWidgetsMap.empty() || taskWidgetsMap.end() == iter)
    {//clear the root tree
      makeRootWidget(rootNode, node);
      iter = taskWidgetsMap.begin();
    }
  assert(iter != taskWidgetsMap.end());

  QTreeWidgetItem* pwidget = iter->second.widget;
  //create widgets for freshly created tree's branch in LinkedTask
  QTreeWidgetItem* lastItem = nullptr;
  std::function<void(WebGrep::LinkedTask*)> makeWItem
      = [this, &pwidget, &lastItem](WebGrep::LinkedTask* _node)
  {
      if(!_node->grepVars.pageIsReady)
        { return; }
      QTreeWidgetItem* item = new QTreeWidgetItem((QTreeWidgetItem*)pwidget);
      Widget::describe(this->guiTempString, _node);
      item->setText(0, this->guiTempString);
      pwidget->addChild(item);
      lastItem = item;

      //register widget<->task connection in the map
      makeKnown(_node, lastItem, this->mainNode);
    };
  //add consequent nodes
  WebGrep::ForEachOnBranch(node, makeWItem, 0);

  //update text information for previously added level:
  if (nullptr == parent)
    { return; }
  parent = WebGrep::ItemLoadAcquire(parent->parent);
  if (nullptr == parent || 0 == parent->level)
    { return; }
  iter = taskWidgetsMap.find(parent);
  assert(taskWidgetsMap.end() != iter);
  pwidget = iter->second.widget;
  WebGrep::LinkedTask* _lt = parent;
  for(int z = 0; z < pwidget->childCount() && nullptr != _lt; ++z, _lt = WebGrep::ItemLoadAcquire(_lt->next))
    {
      QTreeWidgetItem* item = pwidget->child(z);
      Widget::describe(this->guiTempString, _lt);
      item->setText(0, this->guiTempString);
    }
}
//-----------------------------------------------------------------------------
void Widget::onPagesListScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask* node)
{
  if (nullptr == rootNode)
    return;

  std::cerr << "--------------------------\n";
  WebGrep::ForEachOnBranch(node, [](WebGrep::LinkedTask* _node)
  { std::cerr << "\n" << _node->grepVars.targetUrl << "\n";}, 0);

  //------- ---->>>>
  //Make a functor that updates GUI with new algorithm's data.
  //Then dispatch it via queued Qt connection.
  auto ftor = std::function<void()>( [this, rootNode, node]()
    { this->updateRenderNodes(rootNode, node); });

  //dispatch ftor() to be executed in Qt main loop, it'll rearrange the tree's widgets
  QMetaObject::invokeMethod(this, "onFunctor", Qt::QueuedConnection, Q_ARG(Functor_t, ftor) );
  //<<<<--- CRITICAL SECTION ---------
}
//-----------------------------------------------------------------------------
void Widget::onStart()
{
  bool ok = false;
  int nlinks = ui->maxLinksEdit->text().toInt(&ok);
  if (!ok || nlinks <= 0)
    {
      ui->textEdit->setText("Error: wrong max. links value. Please, correct it.");
      ui->maxLinksEdit->clear();
      return;
    }
  if (ui->addressEdit->text().isEmpty())
    {
      ui->textEdit->setText("Error: host[:port] address required!");
      return;
    }
  QString input = ui->textEdit->toPlainText();
  if (input.contains("Error: "))
    {
      ui->textEdit->clear();
      return;
    }
  //start and get the handle to tree of LinkedTask* nodes:
  this->mainNode =
      crawler->start(ui->addressEdit->text().toStdString(),
                     ui->textEdit->toPlainText().toStdString(),
                     nlinks, ui->dial->value());
}
//-----------------------------------------------------------------------------
void Widget::onStop()
{
  crawler->stop();
}
//-----------------------------------------------------------------------------
void Widget::onFunctor(Functor_t func)
{
  func();
}

void Widget::onDialValue(int value)
{
  crawler->setThreadsNumber((unsigned)value);
}

void Widget::paintEvent(QPaintEvent *event)
{
  std::shared_ptr<QString> msg = bufferedErrorMsg;
  if (nullptr != msg)
    {
      ui->textEdit->setText(QString("Error occured:") + *msg);
      bufferedErrorMsg.reset();
    }
  if (nullptr != mainNode)
    {
      auto nodeRef = mainNode;
      auto count = nodeRef->linksCounterPtr->load();
      float r = (float)count;
      r /= (float)nodeRef->maxLinksCountPtr->load();
      r *= 100;
      ui->progressBar->setValue(std::min(100, (int)r));
      guiTempString.sprintf("%u URLs processed.", count);
      ui->urlCountLabel->setText(guiTempString);
    }

  QWidget::paintEvent(event);
}
//-----------------------------------------------------------------------------
void Widget::onHelpClicked()
{
  QString help = "Enter http:// or http:// address to recursively scan pages for URL.\n";
  help.append("\nThe round knob regulates quantity of threads used to process tasks asynchronously(mostly).");
  help.append("\nThe effect of changing threads number is applied immediately, will temporarly suspend scanning of the HTML data.");
  help.append("\nThe progress bar shows percentage of (current URL count / max. URL number) relation.");
  help.append("\nOnce the URL counter reach maximum value the recursive search will be set to idle start.");
  help.append("\nClick \"Start\" button for every new HTTP destination you want to scan.");
  help.append("\"Stop\" button temporarly stops the scan process, use \"Start\" to continue.");
  help.append("\n\n Bohdan Maslovskyi  https://github.com/blinkenlichten/test03-03");
  textDraw->setPlainText(help);
  ui->tabWidget->setCurrentIndex((int)WIDGET_TAB_IDX::TEXT_MATCH_RENDER);
}
