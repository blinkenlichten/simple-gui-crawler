# What's this? Just a simle HTTP(S) crawler
The algorithmic part in subdirectory webgrep/ is intended to get HTML content
and perform simple parsing, webgrep/ depends on Boost(system,thread)
and NEON(in linux neon-devel, and HTTP & WebDAV client library).

The application's GUI is built with Qt5.7 GUI framework.

# Purpose
Take a input URL from the GUI and scan all links to html pages inside recursively and concurrently.
The user must be able to input the URL, number of threads, maximum number of URLs to be scanned.

# Compilation
## Linux
+ Install boost libraries (modules: system, regex, thread, atomic) into system path or
write additional keys to the .pro project file.
+ Compile NEON http client library:
```
wget ihttp://www.webdav.org/neon/neon-0.30.1.tar.gz
export PROJECT=$PWD
mkdir -p $PROJECT/3rdparty
tar xvf neon-0.30.1.tar.gz
cd neon-0.30.1 && ./configure --prefix=$PROJECT/3rdparty --enable-static
make -j$(nproc) && make install
```
Or install it from Linux package system and modify .pro file to use shared library.

## Windows


# Status
The algorithm is working with minimal use of mutex locking, it needs testing.

# Ideas
The program contains not a single explicitly defined mutex or spinlock,
we're syncronizing the tasks either using RAII ownership passing,
volatile bool flags and or higher
level task management entities: boost::base_threadpool (waits on condition),
Qt5 events loop in main thread (same thing).
They're using mutex lock + condition wait and it's just fine.
All other entities do not block when resources have to be passed between threads,
they pass things encapsulated in std::shared_ptr or other data structures.

Internally to track the tasks I'm using tree linked list with atomic pointers to be able to read the list
while it's being processed and appended concurrently (but only with 1 thread as producer).
So, we have 1 producer -> multiple readers here, the readers do not dispose items explicitly,
it is managed by a shared pointers.

```
//from file webgrep/linked_task.h
/** LinkedTask : a tree list that is using atomic pointers
*  for child nodes (to be able to read without locking).
*  Once the node is constructed -- it is okay to read tree pointers concurrently and modify them,
*  the variable (GrapVars grepVars) must be carefully accessed on callbacks
*  when in event-driven flow,
*  (volatile bool)grepVars.pageIsReady indicates that grepVars.pageContent is constructed;
*  (volatile bool)grepVars.pageIsParsed indicates that grepVars.pageContent is already parsed
*  and grepVars.matchURLVector is filled;
*  Instead of processing the task again on current item,
*  better construct new node and move the data there safely.
*/
```
Main idea is to use RAII for top-level objects that operate and communecate via
with smaller objects and functors,
that is mostly enque/deque and sequential functor execution.

The class Crawler in "webgrep/crawler.h[.cpp]" works with the list using boost::basic_threadpool
by pushing a functor objects in it and providing the results in a callbacks.
This is kind of transactional ways of calling routines.
It is hard to write it the right way from first try,
but as a result you get a versatile task distribution system
without inventing more and more types to describe events, methods to serialize them etc.

Whole parsing boiler plate (with boost::regex) is located in files "webgrep/crawler_worker.h[.cpp]",
it also has methods that define the program flow: spawn new tasks until there is such need for the
algorithm.

# GUI and the algorithm
Ideally, I could write some kind of adapter class instead of working with bare LinkedTask* pointers,
but it'll a bit more effort.
So, in GUI we just read the reference counted pointer std::shared_ptr<LinkedTask> which has a custom
deleter and update the GUI elements that shows the progress of the scanning process.


![Screenshot](https://raw.githubusercontent.com/blinkenlichten/test03-v03/master/images/screenshot.png)

