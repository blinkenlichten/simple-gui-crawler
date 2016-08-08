#include "widget.h"
#include "ui_widget.h"

#include <mutex>
#include <QMetaObject>
#include "webgrep/crawler.h"
#include "webgrep/linked_task.h"
#include "link_status_widget.h"

//-----------------------------------------------------------------------------
Widget::Widget(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Widget)
{
  ui->setupUi(this);
  setLayout(ui->topLayout);

  ui->groupBoxText->setLayout(ui->horizontalLayoutText);
  QTabWidget* tabw = ui->tabWidget;
  QWidget* textWidget = tabw->widget((int)WIDGET_TAB_IDX::FOUND_TEXT);
  textWidget->setLayout(ui->listVerticalLayout_2);

  QWidget* pageWidget = tabw->widget((int)WIDGET_TAB_IDX::PAGE_RENDER);
  pageWidget->setLayout(ui->pageHorizontalLayout);

  QWidget* graphWidget = tabw->widget((int)WIDGET_TAB_IDX::GRAPH_RENDER);
  graphWidget->setLayout(ui->graphHorizontalLayout);
  ui->groupBoxText->setMaximumHeight(60);

  webPage = new QTextEdit(pageWidget);
  ui->pageHorizontalLayout->addWidget(webPage);

  tabw->setCurrentIndex(0);

  guiTempString.reserve(256);

  crawler = std::make_shared<WebGrep::Crawler>();


  connect(ui->buttonStart, &QPushButton::clicked,
          this, &Widget::onStart, Qt::DirectConnection);

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
void Widget::onList2Clicked(QListWidgetItem* item)
{
  LinkStatusWidget* st = (LinkStatusWidget*)item;
  //render scanned web-page
  if (nullptr != st->node) {
      this->webPage->setHtml(QString::fromStdString(st->node->grepVars.pageContent));
      ui->tabWidget->setCurrentIndex((int)WIDGET_TAB_IDX::PAGE_RENDER);
      QWidget* render = this->ui->tabWidget->widget((int)WIDGET_TAB_IDX::GRAPH_RENDER);
      render->update();
    }
}
//-----------------------------------------------------------------------------
void Widget::onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask* node)
{
  /** Spawns items that display status of page scann.**/
  auto spawnFunctor = [this](WebGrep::LinkedTask* head, void*)
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
          };

  static boost::detail::spinlock guiLock;
  std::lock_guard<boost::detail::spinlock> lk(guiLock); (void)lk;

  ui->listWidgetPending->clear();

  if (this->mainNode != rootNode)
    {//case the tree is new,
      ui->listWidgetReady->clear();
      WebGrep::TraverseFunctor(rootNode.get(), nullptr, spawnFunctor);
      this->mainNode = rootNode;
      return;
    }
  //case working with previous items tree
  ui->listWidgetPending->clear();
  WebGrep::TraverseFunctor(node, nullptr, spawnFunctor);
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
