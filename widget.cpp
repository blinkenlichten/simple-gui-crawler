#include "widget.h"
#include "ui_widget.h"

#include <mutex>
#include <QTimer>
#include <QMetaObject>
#include <functional>
#include <QTextEdit>
#include <QTextCodec>
#include "webgrep/crawler.h"
#include "webgrep/linked_task.h"
#include "link_status_widget.h"

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

  //check for tasks status sometimes
  checkOutTimer = new QTimer(this);
  connect(checkOutTimer, &QTimer::timeout, this, &Widget::onCheckOutTimer, Qt::DirectConnection);
  checkOutTimer->start(2000);

  connect(ui->helpButton, &QPushButton::clicked, this, &Widget::onHelpClicked);

  //on click in second list -> show downloaded .html web page
  connect(ui->listWidgetReady, &QListWidget::itemActivated,
          this, &Widget::onList2Clicked, Qt::DirectConnection);

  crawler->setExceptionCB
      ( [this] (const std::string& what) { bufferedErrorMsg = std::make_shared<QString>(QString::fromStdString(what)); } );


  /** On new linked page scan finished.*/
  crawler->setPageScannedCB([this]
  (std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask* node)
  {
    this->onPageScanned(rootNode, node);
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

void Widget::populateListsFunction(WebGrep::LinkedTask* head, void*)
{
  const WebGrep::GrepVars& g(head->grepVars);
  QListWidget* listPtr = nullptr;
  if (!g.pageIsParsed)
    {
      guiTempString.sprintf("url: %s (GET code: %d) (Status: %s )",
                            g.targetUrl.data(), g.responseCode,
                            g.pageIsReady? "downloaded" : "pending in the queue");
      listPtr = ui->listWidgetPending;
    }
  else
    {
      guiTempString.sprintf("url: %s (GET code: %d) (Status:parsed) (Text Matches: %u) (URL matches: %u)",
                            g.targetUrl.data(), g.responseCode,
                            g.matchTextVector.size(), g.matchURLVector.size());

      listPtr = ui->listWidgetReady;
    }
  auto item = new LinkStatusWidget(listPtr);
  item->rootTask = this->mainNode;
  item->node = head;

  item->setText(guiTempString);
}

//-----------------------------------------------------------------------------
void Widget::onList2Clicked(QListWidgetItem* item)
{
  LinkStatusWidget* st = (LinkStatusWidget*)item;
  if (nullptr == st->node) {
      return;
    }

  //render scanned web-page
  const WebGrep::GrepVars& g(st->node->grepVars);
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
  ui->listWidgetPending->clear();
  ui->listWidgetReady->clear();
  onPageScanned(mainNode, mainNode.get());
}
//-----------------------------------------------------------------------------
void Widget::onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask* node)
{
  (void)node;
  if (nullptr == rootNode)
    return;

  float r = (float)rootNode->linksCounterPtr->load();
  r /= (float)rootNode->maxLinksCountPtr->load();
  r *= 100;

  /** Spawns items that display status of page scan.**/
  auto funcListsFill = [this](WebGrep::LinkedTask* head, void* additional)
  { this->populateListsFunction(head, additional); };

  //------- CRITICAL SECTION ---->>>>
  //dispatch a functor that has some GUI things to be done
  auto ftor = std::function<void()>(
                    [this, r, rootNode, funcListsFill]()
    {
      ui->progressBar->setValue(r);
      ui->listWidgetPending->clear();

      if (this->mainNode != rootNode)
        {//case the tree is new,
          ui->listWidgetReady->clear();
          mainNode = rootNode;
        }
      //case working with previous items tree
      WebGrep::TraverseFunctor(mainNode.get(), nullptr, funcListsFill);

    });

  QMetaObject::invokeMethod(this, "onFunctor", Qt::QueuedConnection,
                            Q_ARG(Functor_t, ftor) );
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
  crawler->start(ui->addressEdit->text().toStdString(),
                 ui->textEdit->toPlainText().toStdString(),
                 nlinks, ui->dial->value());
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
