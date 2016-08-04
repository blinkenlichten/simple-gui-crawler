#include "linked_task.h"
#include <cassert>
#include <iostream>

namespace WebGrep {
//---------------------------------------------------------------
void LinkedTask::shallowCopy(const LinkedTask& other)
{
  level = other.level;
  root = other.root;
  parent = other.parent;
  {
    const GrepVars& og(other.grepVars);
    grepVars.grepExpr = og.grepExpr;
//    grepVars.allocatorPtr = og.allocatorPtr;
  }

  maxLinkCount = other.maxLinkCount;
  linksCounterPtr = other.linksCounterPtr;
  childLevelSpawned = other.childLevelSpawned;
  pageMatchFinishedCb = other.pageMatchFinishedCb;
}

size_t LinkedTask::spawnChildNodes(size_t nodesCount)
{
  if (0 == nodesCount)
    return 0;

  size_t spawnCnt = 0;
  //backup pointer to exsiting childs:
  LinkedTask* existing_bkup = ItemLoadAcquire(this->child);
  LinkedTask* item = nullptr;
  try {
    //spawn subtree list's head:
    this->child.store((std::uintptr_t)(new LinkedTask), std::memory_order_acquire);
    ++spawnCnt;
    item = (LinkedTask*)child.load();
    {
      item->shallowCopy(*this);
      item->parent = this;
      item->level = 1u + this->level;
      item->order = this->childNodesCount.load(std::memory_order_acquire);
      this->childNodesCount.fetch_add(1);
    }

    //spawn items on same level (access by .next)
    do
      {
        item->next.store((std::uintptr_t)(new LinkedTask), std::memory_order_acquire);
        item = (LinkedTask*)item->next.load(std::memory_order_relaxed);
        {
          item->shallowCopy(*this);
          item->parent = this;
          item->level = 1u + this->level;
          item->order = this->childNodesCount.load(std::memory_order_acquire);
          this->childNodesCount.fetch_add(1);
        }
      } while(++spawnCnt < nodesCount);
  } catch(std::exception& ex)
  {
    std::cerr << __FUNCTION__ << std::endl;
    std::cerr << ex.what() << std::endl;
  }
  //now node is a pointer to last element or zero
  //put previously spawned child nodes to the end of a new list:
  if (nullptr != existing_bkup && nullptr != item)
    {
      item->next.store((std::uintptr_t)existing_bkup, std::memory_order_acquire);
    }

  return spawnCnt;
}

size_t LinkedTask::spawnGreppedSubtasks()
{
  size_t spawnedCnt = spawnChildNodes(grepVars.matchURL.size());
  LinkedTask* node = ItemLoadAcquire(child);
  for(size_t c = 0; c < spawnedCnt && nullptr != node;
      ++c, node = ItemLoadAcquire(node->next))
    {
      (node->grepVars.targetUrl) = grepVars.matchURL[c];
    }
  return spawnedCnt;
}

//---------------------------------------------------------------

}//WebGrep
