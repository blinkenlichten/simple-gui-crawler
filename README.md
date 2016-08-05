# What's this? Just a simle HTTP(S) crawler
The algorithmic part in subdirectory webgrep/ depends on Boost(system,thread)
and NEON(in linux neon-devel, and HTTP & WebDAV client library.)

The application is built with Qt5.7 GUI framework.

# Purpose
Take a input URL from the GUI and scan all links to html pages inside recursively and concurrently.
The user must be able to input the URL, number of threads, maximum number of URLs to be scanned.

# Status
The algorithm is working with minimal use of mutex locking, it needs testing, bugfixing & bindings to the GUI application part.

# Ideas
Internally I'm using tree linked list with atomic pointers to be able to read the list
while it's being processed and appended concurrently.

```
//from file webgrep/linked_task.h
/** LinkedTask : a tree list that is using atomic pointers
 *  for child nodes (to be able to read without locking).
 *  Once the node is constructed -- it is okay to read tree pointers concurrently,
 *  the variable (GrapVars grepVars) must be carefully accessed on callbacks
 *  when (volatile bool)grepVars.pageIsParse has been set, for example.
 */
```

The class Crawler in "webgrep/crawler.h[.cpp]" works with the list using boost::basic_threadpool
by pushing a functor objects in it and providing the results in a callbacks.
This is kind of transactional ways of calling routines.

Whole parsing boiler plate (with boost::regex) is located in files "webgrep/crawler_worker.h[.cpp]"


![Screenshot](https://raw.githubusercontent.com/blinkenlichten/test03-v03/master/images/screenshot.png)

