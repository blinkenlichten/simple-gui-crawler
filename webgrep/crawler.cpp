#include "crawler.h"
#include <iostream>

namespace WebGrep {

class Crawler::CrawlerPV
{
public:
  CrawlerPV()
  {

  }

};

Crawler::Crawler()
{
  pv.reset(new CrawlerPV);
}

}
