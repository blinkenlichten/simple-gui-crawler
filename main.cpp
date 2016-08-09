#include "widget.h"
#include <QApplication>
#include "webgrep/crawler.h"

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
  Widget w;
  w.show();

  return a.exec();
}
