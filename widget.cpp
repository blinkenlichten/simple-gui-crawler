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

  ui->setupUi(this);
  setLayout(ui->topLayout);

  ui->groupBoxText->setLayout(ui->horizontalLayoutText);
  QTabWidget* tabw = ui->tabWidget;
  QWidget* listsWidget = tabw->widget((int)WIDGET_TAB_IDX::FOUND_TEXT);
  listsWidget->setLayout(ui->listVerticalLayout_2);

  QWidget* pageWidget = tabw->widget((int)WIDGET_TAB_IDX::PAGE_RENDER);
  pageWidget->setLayout(ui->pageHorizontalLayout);

  QWidget* graphWidget = tabw->widget((int)WIDGET_TAB_IDX::GRAPH_RENDER);
  graphWidget->setLayout(ui->graphHorizontalLayout);
  ui->groupBoxText->setMaximumHeight(60);

  webPage = new QTextEdit(pageWidget);
  ui->pageHorizontalLayout->addWidget(webPage);

  auto textWidget = tabw->widget((int)WIDGET_TAB_IDX::TEXT_RENDER);
  textWidget->setLayout(ui->verticalLayoutTextMatches);
  textDraw = new QTextEdit(textWidget);
  textDraw->setAcceptRichText(false);
  ui->verticalLayoutTextMatches->addWidget(textDraw);

  tabw->setCurrentIndex(0);

  guiTempString.reserve(256);

  crawler = std::make_shared<WebGrep::Crawler>();


  connect(ui->buttonStart, &QPushButton::clicked,
          this, &Widget::onStart, Qt::DirectConnection);

  //check for tasks status sometimes
  checkOutTimer = new QTimer(this);
  connect(checkOutTimer, &QTimer::timeout, this, &Widget::onCheckOutTimer, Qt::DirectConnection);
  checkOutTimer->start(2000);

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
      guiTempString.sprintf("url: %s (GET code: %d) (Status:parsed) (Text Matches: %lu) (URL matches: %lu)",
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
//We'll not eat too much CPU cycles, I promise!
static boost::detail::spinlock guiLock;
//-----------------------------------------------------------------------------

void Widget::onCheckOutTimer()
{
  {
    //------- CRITICAL SECTION ---->>>>
    std::lock_guard<boost::detail::spinlock> lk(guiLock); (void)lk;
    ui->listWidgetPending->clear();
    ui->listWidgetReady->clear();
    //<<<<--- CRITICAL SECTION ---------
  }
  onPageScanned(mainNode, mainNode.get());
}
//-----------------------------------------------------------------------------
void Widget::onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask* node)
{
  /** Spawns items that display status of page scan.**/
//  auto funcListsFill = std::bind(&Widget::populateListsFunction, this, node, (void*)nullptr);
  auto funcListsFill = std::bind(&Widget::print, this, node, (void*)nullptr);
  //------- CRITICAL SECTION ---->>>>
  std::lock_guard<boost::detail::spinlock> lk(guiLock); (void)lk;
  ui->listWidgetPending->clear();

  if (this->mainNode != rootNode)
    {//case the tree is new,
      ui->listWidgetReady->clear();
      mainNode = rootNode;
    }
  //case working with previous items tree
  std::cerr << "*******************************\n";
  WebGrep::TraverseFunctor(mainNode.get(), nullptr, funcListsFill);
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
  if (input.isEmpty() || input.contains("Error: "))
    {
      ui->textEdit->setText("Hint: please, enter exact word or Perl syntax regexp. here");
      return;
    }
  if (input.contains("Hint: "))
    return;
  crawler->start(ui->addressEdit->text().toStdString(),
                 ui->textEdit->toPlainText().toStdString(),
                 nlinks, ui->dial->value());
}
//-----------------------------------------------------------------------------
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
