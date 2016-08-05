#ifndef CRAWLER_H
#define CRAWLER_H

#include <memory>

namespace WebGrep {

class LinkedTask;

//----
class Crawler
{
public:
  Crawler();

  //set threads number, not lesst than 1
  //will stop some excessive threads if they're running
  void setThreadsNumber(unsigned nthreads = 1);
  void setMaxLinks(unsigned maxScanLinks = 4096);

  /** @param grepRegex: boost.regex valid expression to grep the textual content.*/
  bool start(const std::string& url, const std::string& grepRegex,
             unsigned maxLinks = 4096, unsigned threadsNum = 4);
  void pause();
  void stop();
  void clear();


  std::function<void(const std::string& what)> onException;

  //provides access no newly spawned tree node(one for each page)
  //the (LinkedTask*) can be thread concurrently under some circumstances (see it's doc)
  std::function<void(LinkedTask* rootNode, LinkedTask* node)> onPageScanned;
private:
  class CrawlerPV;
  std::shared_ptr<CrawlerPV> pv;
};

}//WebGrep

#endif // CRAWLER_H
