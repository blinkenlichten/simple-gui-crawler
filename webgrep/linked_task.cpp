#include "linked_task.h"

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

//---------------------------------------------------------------

}//WebGrep
