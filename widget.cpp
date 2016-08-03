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
}

Widget::~Widget()
{
  delete ui;
}

void Widget::onStart()
{
  crawler->start("https://http://en.cppreference.com/w/cpp/atomic/atomic",
                 "?(fetch)", 8, 16);
}
