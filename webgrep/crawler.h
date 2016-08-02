#ifndef CRAWLER_H
#define CRAWLER_H

#include <memory>

namespace WebGrep {

//----
class Crawler
{
public:
  Crawler();

  //set threads number, not lesst than 1
  //will stop some excessive threads if they're running
  void setThreadsNumber(unsigned nthreads = 1);


private:
  class CrawlerPV;
  std::shared_ptr<CrawlerPV> pv;
};

}//WebGrep

#endif // CRAWLER_H
