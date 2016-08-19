#ifndef CRAWLER_H
#define CRAWLER_H

#include <memory>

namespace WebGrep {

class LinkedTask;
class CrawlerPV;

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

  static const unsigned maxThreads = 32;

  /** set threads number, not less than 1.
   *  Will be apply immediately and start new threads or kill some old threads. */
  void setThreadsNumber(unsigned nthreads = 1);

  /** set links count any time. */
  void setMaxLinks(unsigned maxScanLinks = 4096);

  /** Start recursive scanning of the URLs from given page
   * It will continue scanning from the last stop() point if the arguments
   * are the same as before.
   *
   * @param grepRegex: boost.regex valid expression to grep the textual content.*/
  bool start(const std::string& url, const std::string& grepRegex,
             unsigned maxLinks = 4096, unsigned threadsNum = 4);

  /** Halts the html pages crawler for a while.
   *  Use clear() to clear the search results totally.*/
  void stop();

  /** Clear the search results.*/
  void clear();


  //set this to handle internal exceptions(like std::bad_alloc), must not throw
  typedef std::function<void(const std::string& what)> OnExceptionCallback_t;

  /** The functor type describes access no newly spawned tree node(one for each page)
  the (LinkedTask*) can be read concurrently under some circumstances (see it's doc).
  The pointer (LinkedTask*)node is valied until std::shared_ptr<LinkedTask> rootNode exists. */
  typedef std::function<void(std::shared_ptr<LinkedTask> rootNode, LinkedTask* node)> OnPageScannedCallback_t;

  void setExceptionCB(OnExceptionCallback_t func);
  void setPageScannedCB(OnPageScannedCallback_t func);
  void setLevelSpawnedCB(OnPageScannedCallback_t func);

private:
  std::shared_ptr<CrawlerPV> pv;
};

}//WebGrep

#endif // CRAWLER_H
