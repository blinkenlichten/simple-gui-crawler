#include "crawler.h"
#include <iostream>
#include "crawler_worker.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace WebGrep {

class Crawler::CrawlerPV
{
public:
  CrawlerPV()
  {
    workers.reserve(16);
    workers.push_back(std::make_shared<WebGrep::Worker>());
  }

  virtual ~CrawlerPV()
  {
    std::lock_guard<std::mutex> lk(wlistMutex); (void)lk;
    for(std::shared_ptr<WebGrep::Worker>& val : workers)
      {
        val->stop();
      }
  }


  std::mutex wlistMutex;
  std::vector<std::shared_ptr<WebGrep::Worker>> workers;
};

Crawler::Crawler()
{
  pv.reset(new CrawlerPV);
}

void Crawler::setThreadsNumber(unsigned nthreads)
{
  if (0 == nthreads)
    {
      std::cerr << "void Crawler::setThreadsNumber(unsigned nthreads) -> 0 value ignored.\n";
      return;
    }
  std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;

  typedef std::vector<WebGrep::WorkerCommand> CmdVector_t;
  std::vector<CmdVector_t> taskLeftOvers;

  if (nthreads < pv->workers.size())
    {
      taskLeftOvers.resize(pv->workers.size() - nthreads);
      for(unsigned cnt = 0; cnt < nthreads; ++cnt)
        {
          taskLeftOvers[cnt] = (pv->workers[cnt])->stop();
        }
    }
  pv->workers.resize(nthreads);
  for(std::shared_ptr<WebGrep::Worker>& val : pv->workers)
    {
      val->start();
    }
  //put leftover tasks into other threads
  for(size_t item = taskLeftOvers.size(); item > 0; )
    {
      --item;
      CmdVector_t& vec(taskLeftOvers[item]);
      pv->workers[item % (pv->workers.size() - 1)]->put(&vec[0], vec.size());
    }
}

}//WebGrep
