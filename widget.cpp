#include "widget.h"
#include "ui_widget.h"

Widget::Widget(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Widget)
{
  ui->setupUi(this);
  ui->groupBoxText->setLayout(ui->horizontalLayoutText);
  QTabWidget* tabw = ui->tabWidget;
  QWidget* textWidget = tabw->widget((int)WIDGET_TAB_IDX::FOUND_TEXT);
  textWidget->setLayout(ui->textHorizontalLayout);

  QWidget* pageWidget = tabw->widget((int)WIDGET_TAB_IDX::PAGE_RENDER);
  pageWidget->setLayout(ui->pageHorizontalLayout);

  QWidget* graphWidget = tabw->widget((int)WIDGET_TAB_IDX::GRAPH_RENDER);
  graphWidget->setLayout(ui->graphHorizontalLayout);
  ui->groupBoxText->setMaximumHeight(60);

  crawler = std::make_shared<WebGrep::Crawler>();

  connect(ui->buttonStart, &QPushButton::clicked,
          this, &Widget::onStart, Qt::DirectConnection);
  crawler->onException =  [this] (const std::string& what)
  {
      bufferedErrorMsg = std::make_shared<QString>(QString::fromStdString(what));
  };
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
  QWidget::paintEvent(event);
}
