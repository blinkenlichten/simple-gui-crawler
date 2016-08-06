#ifndef CRAWLER_H
#define CRAWLER_H

#include <memory>

namespace WebGrep {

class LinkedTask;

//----
/** The crawler: download first HTML page, grep http:// or href=
 * resources from there and start recursive traversal of those links.
 * Currently only file endings (types) supported: .htm, .php, "/resource_name"
 *
 * All class methods and the construcor will catch any internal exceptions and call onException()
 * callback.
*/
class Crawler
{
public:
  Crawler();

  //set threads number, not lesst than 1
  //start() is needed to continue where we've stopped on setThreadsNumber()
  void setThreadsNumber(unsigned nthreads = 1);

  //set links count before start()
  void setMaxLinks(unsigned maxScanLinks = 4096);

  /** Start recursive scanning of the URLs from given page
   * It will continue scanning from the last stop() point if the arguments
   * are the same as before.
   *
   * @param grepRegex: boost.regex valid expression to grep the textual content.*/
  bool start(const std::string& url, const std::string& grepRegex,
             unsigned maxLinks = 4096, unsigned threadsNum = 4);

  /** Halts the html pages crawler.
   *  Use clear() to clear the search results totally.*/
  void stop();

  /** Clear the search results.*/
  void clear();


  //set this to handle internal exceptions(like std::bad_alloc), must not throw
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
