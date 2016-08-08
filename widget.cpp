#include "widget.h"
#include "ui_widget.h"

#include <mutex>
#include "webgrep/linked_task.h"

//---------------------------------------------------------------
static boost::detail::spinlock guiSpinLock;
static QString guiTempString;
//static std::vector<QString> guiList1Vector, guiList2Vector;
//---------------------------------------------------------------
void GuiTraverseFunctor(WebGrep::LinkedTask* head, void* data)
{
  Widget* w = (Widget*)data;
  const WebGrep::GrepVars& g(head->grepVars);
  if (!g.pageIsParsed)
    {
      guiTempString.sprintf("url: %s (GET code: %d) (Status: %s )",
                            g.targetUrl.data(), g.responseCode,
                            g.pageIsReady? "downloaded" : "pending in the queue");
      w->ui->listWidget->addItem(guiTempString);
    }
  else
    {
      guiTempString.sprintf("url: %s (GET code: %d) (Status:parsed) (Text Matches: %lu) (URL matches: %lu)",
                            g.targetUrl.data(), g.responseCode,
                            g.matchTextVector.size(), g.matchURLVector.size());

      w->ui->listWidget_2->addItem(guiTempString);
    }
}


Widget::Widget(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Widget)
{
  ui->setupUi(this);
  ui->groupBoxText->setLayout(ui->horizontalLayoutText);
  QTabWidget* tabw = ui->tabWidget;
  QWidget* textWidget = tabw->widget((int)WIDGET_TAB_IDX::FOUND_TEXT);
  textWidget->setLayout(ui->listVerticalLayout_2);

  QWidget* pageWidget = tabw->widget((int)WIDGET_TAB_IDX::PAGE_RENDER);
  pageWidget->setLayout(ui->pageHorizontalLayout);

  QWidget* graphWidget = tabw->widget((int)WIDGET_TAB_IDX::GRAPH_RENDER);
  graphWidget->setLayout(ui->graphHorizontalLayout);
  ui->groupBoxText->setMaximumHeight(60);

//  guiList1Vector.reserve(1024);
//  guiList2Vector.reserve(1024);
  guiTempString.reserve(256);

  crawler = std::make_shared<WebGrep::Crawler>();

  connect(ui->buttonStart, &QPushButton::clicked,
          this, &Widget::onStart, Qt::DirectConnection);
  crawler->setExceptionCB
      (
        [this] (const std::string& what)
        {
            bufferedErrorMsg = std::make_shared<QString>(QString::fromStdString(what));
        }
  );
  crawler->setPageScannedCB([this](std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask* node)
  {
    std::lock_guard<boost::detail::spinlock> lkgui(guiSpinLock); (void)lkgui;
//    ui->listWidget->clear();
//    ui->listWidget_2->clear();

    std::lock_guard<WebGrep::PageLock_t> lk(node->grepVars.pageLock);
    WebGrep::TraverseFunc(node, (void*)this, GuiTraverseFunctor);
//    WebGrep::TraverseFunctor(rootNode.get(), nullptr,
//                             [this](WebGrep::LinkedTask* head, void*)
//    {
//      const WebGrep::GrepVars& g(head->grepVars);
//      if (!g.pageIsParsed)
//        {
//          guiTempString.sprintf("url: %s (GET code: %d) (Status: %s )",
//                                g.targetUrl.data(), g.responseCode,
//                                g.pageIsReady? "downloaded" : "pending in the queue");
//          ui->listWidget->addItem(guiTempString);
//        }
//      else
//        {
//          guiTempString.sprintf("url: %s (GET code: %d) (Status:parsed) (Text Matches: %lu) (URL matches: %lu)",
//                                g.targetUrl.data(), g.responseCode,
//                                g.matchTextVector.size(), g.matchURLVector.size());

//          ui->listWidget_2->addItem(guiTempString);
//        }
//    }
//    );
  });
}

Widget::~Widget()
{
  delete ui;
}

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

void Widget::paintEvent(QPaintEvent *event)
{
  std::shared_ptr<QString> msg = bufferedErrorMsg;
  if (nullptr != msg)
    {
      ui->textEdit->setText(QString("Error occured:") + *msg);
      bufferedErrorMsg.reset();
    }
  std::lock_guard<boost::detail::spinlock> lk(guiSpinLock); (void)lk;
  QWidget::paintEvent(event);
}
