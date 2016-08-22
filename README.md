# What's this? Just a simle HTTP(S) crawler
The algorithmic part in subdirectory webgrep/ is intended to get HTML content
and perform simple parsing, webgrep/ depends on Boost(system,thread)
and NEON(in linux neon-devel, and HTTP & WebDAV client library).

The application's GUI is built with Qt5.7 GUI framework.

# Purpose
Take a input URL from the GUI and scan all links to html pages inside recursively and concurrently.
The user must be able to input the URL, number of threads, maximum number of URLs to be scanned.

# Compilation
The project consists of main test03-v03/CMakeLists.txt file that makes the webgrepGUI Qt5 GUI application
and subproject test03-v03/webgrep/CMakeLists.txt that makes libwebgrep library containing
the WEB crawler's algorithm.

## Linux dependencies
+ Install openSSL(development) and into system path or
write additional keys to the .pro project file.
It is enough to have either cURL or NEON development package in the system
or in project/3rdparty subdirectory installed and pass the appropriate variable to CMAke.

## Windows dependencies
Download CURL DLL distribution for MinGW from here https://bintray.com/artifact/download/vszakats/generic/curl-7.50.1-win32-mingw.7z
Put the libcurl.dll.a into 3rdparty/lib and the headers to 3rdparty/include then compile the project.

## Project build: CMake & Make
```
cd test03-v03
mkdir build
cd build
cmake .. -DQT5_SEARCH_PATH="D:/Qt/5.7"
make -j4
```
Possible CMake options:
```
-DQT5_SEARCH_PATH="/qt_path"     # path where CMake will look for Qt5
-DDEPENDS_ROOT="/path"  #additional path where we look for cURL or NEON
-DUSE_LIBNEON         #use NEON (default on Linux/Mac)
-DUSE_LIBCURL         #use cURL (default on Windows)
-DUSE_QTNETWORK       #dont use cURL or NEON but enable yet buggy experimental code where QtNetwork is used instead
```
There are also unit tests' executables being build.

### Deployment on Windows: OpenSSL and Qt5
Due to different licensing approach, Qt5 does not link to OpenSSL libraries,
instead they load the library at runtime at the path where program.exe is located.
Copy these to the executable's directory: libeay32.dll ssleay32.dll libgcc_s_dw2.dll


# ---True story, bro.---
# Networking: few approches

# Popular things: Microsoft's Cassandra (client/server), Poco libraries
They're nice, but I couldn't afford MSVC to compile them, it always goes wrong with Microsoft's
mutilated free version of MSVC. Thus I had to play around MinGW.

# Using MinGW to build network client applications

As can seen from the repo's history the initial idea was to use boost.asio and it's SSL socket,
but then it went wrong with HTTPS and some exceptions, it needs more tedious work.
So I threw it out and decided to use some kind of C library with straightforward API that will just let you to do the jobs.

# C-API library
My considerations were NEON (http & webdav client) and CURL.
NEON has much simpler API, because it's intended for HTTP(S) only whereas CURL works over almost any protocol and hence has more complex API.
Both of them have UNIX-style configurations system and require to use either MSYS or Cygwin (worked out for neon).
At the moment NEON is ised by default for Linux/Mac systems, libCURL for Win32 for reason.

I had really bad experience trying to compile libneon with MinGW, it neet's some tedious picking up of the dependencies (like OpenSSL or GNUTls).

NEON worked fine when I've compiled it using Cygwin,
but there is *HUGE PROBLEM*, you can't link Qt5 GUI applications built by QtSDK
with Cygwin runtime libraries, there is Qt5 distribution for Cygwin, but it depends on X11/Xcb libraries port for Cygwin
and requires the target deployment to have Cygwin runtime installed into the system which is unacceptable.

Thus, I just download and link to the pre-build libCURL .dll.a from the link on the CURL author's page (https://curl.haxx.se).


# Qt5: QNetworkAccessManager
The program also has use case of Qt5 networking API, but it somehow doesn't call the documented API signals,
might be a Qt's bug or just wrong use case, needs investigation.

Experimental code with QNetworkAccessManager can be turned on on by .pro file variable "VAR_WITH_QTNETWORK=1",
then libneon/libcurl won't be needed ... but the program will fail to download the pages(TODO: fix it later).


# Status
The algorithm and the GUI are, they need some testing.

# Ideas
There are 2 points:
+ test03-v03/webgrep subproject has pure C++11 STL code + external C libraries for HTTP(S), manual thread/callback management.
+ test03-v03/ Has Qt5 GUI with all it's fancy stuff like SLOTs/SIGNALs, although built with CMake, coz qmake syntax is dumb.

The program has reduced to minimum use of explicitly defined sync. variables like mutual exclusions,
we're syncronizing the tasks either using lock-free RAII ownership passing(shared pointers),
and some volatile bool flags, the rest is taken by higher level task management entities: 
+ WebGrep::ThreadsPool  (implemented much like boost::base_threadpool, it just waits on condition),
+ Qt5 events loop in main GUI thread with signals/slots.

They're using mutex lock + condition wait and it's just fine.
All other entities do not block when resources have to be passed between threads,
they pass things encapsulated in std::shared_ptr and functors.

I know the drawbacks of using functors everywhere(too much context switching),
but at least I'm not writing this stuff in JavaScript/PHP with consequent asking of the
employer to buy new mega-computer-server-dozer2000 to make that shit work :-D

## Regular expressions with std::regex
To extract http:// I'm using handwritten methods, std::regex just for some additional methods.
Parsing .html with regular expressions is a common pitfall, so I avoid it after giving up on some tries.
The program also greps the web page to find some text if the user has provided it in 2nd input field of he GUI.

## Linked list with atomic pointers
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

The data structure and it's creation process scheme is:
```
    CrawlerPV::start() gets the input URL and downloads 1 HTML page using WebGrep::FuncGrepOne.
    It will spawn a root node with root.grepVars.targetURL="https://mit.edu" (for example)
    root.grepVars.matchURLVector will contain std::string iterators that point to HTML page contents
    std::string in root.grepVars.pageContent.
    Then it will create 1 child node at root->child
    with child.grepVars.targetURL={URL1 from the page, e.g. root.grepVars.matchURLVector[0]}.
    Then it will spaw consequent nodes at child->next from next matched URLs in range root.grepVars.matchURLVector[1, size()-1]. Now when we have spawned top level of the tree, we call WebGrep::FuncDownloadRecursive() on each top level
    node, the method will run nodes' parsing concurrently using the WebGrep::ThreadsPool class.

    [root(first URL)]
          |
          | {grep page and get array of matched URLs into root.grepVars.matchURLVector}
          | {spawn 1 child with URL1}
          | {spawn a chain of new nodes attached to child with URL1 using
          |  size_t LinkedTask::spawnGreppedSubtasks(const std::string& host_and_port, const GrepVars&, size_t);}
          |
          +--> [child(URL1)]-->[child->next (URL2)]-->[next->next (URL3)]
                    |                   |
                    |                   +-[child2->child{URL2.1}]-----[URL{2.K}]
                    |
                    | {recursive call to bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtx& w)
                    |  for each node spaws new subtasks that create new child nodes.
                    |
                [child->child{URL1.1}]---[URL{1.2}]--... [URL{1.M}]
```

## Components

+ The class Crawler in "webgrep/crawler.h[.cpp]" works with the list of tasks
using a thread manager like the boost::basic_threadpool,for example,
it's pushing a functor objects in it and providing the results in a callbacks.
This is kind of transactional ways of calling routines.
It is hard to write it the right way from first try,
but as a result you get a versatile task distribution system
without inventing more and more types to describe events, methods to serialize them etc.

+ Whole parsing boiler plate (with boost::regex) is located in files "webgrep/crawler_worker.h[.cpp]",
it also has methods that define the program flow: spawn new tasks until there is such need for the
algorithm.

+ The class WebGrep::ThreadsPool resambles is a thread manager that can dispatch std::function<void()>
functors to be executed, it's functionality similar to boost::basic_thread_pool,
which was used previously, but I decided to get rid of boost since only 1 class was used from there.
Unlike boost, ThreadsPool allows one to get the abandoned tasks when the manager has been stopped
and it's threads joined.

+ The class WebGrep::Client is intended to make a GET requests to the remote hosts
via HTTP(S), it encapsulates 3 possible HTTP client "engines" under the hood:
NEON, cURL, QtNetwork::QNetworkAccessManager. They're switched by CMake options.

# GUI and the algorithm
Ideally, I could write some kind of adapter class instead of working with bare (LinkedTask\*) pointers,
but it'll a bit more effort.
The "bandaging" code between the algoritm and the GUI takes nearly 120 lines of code and can be located
at method "widget.cpp: void Widget::onPageScanned(std::shared_ptr<WebGrep::LinkedTask> rootNode, WebGrep::LinkedTask\* node)"
So, in GUI we just read the reference counted pointer std::shared_ptr<LinkedTask> which has a custom
deleter and update the GUI elements that shows the progress of the scanning process.


![Screenshot](https://raw.githubusercontent.com/blinkenlichten/test03-v03/master/images/screenshot.png)


# Legacy/failed experience. The project has been changing much, so these are just hints for future.
Experience of abandoned or failed tries with MinGW/Cygwin etc.

### If you build something against libneon or libcurl under Cygwin for Windows
List of Cygwin runtime dependencies for build under MS Windows & Cygwin,
the item cygneon-27.dll is the one that has been compiled under Cygwin manually.
Note! You can not link other other applications to these libraries, they depend on Cygwin's runtime.

```
cygcom_err-2.dll
cygcrypto-1.0.0.dll
cygexpat-1.dll
cygffi-6.dll
cyggcc_s-1.dll
cyggmp-10.dll
cyggnutls-28.dll
cyggnutls-openssl-27.dll
cyggnutlsxx-28.dll
cyggssapi_krb5-2.dll
cyghogweed-2.dll
cygiconv-2.dll
cygintl-8.dll
cygk5crypto-3.dll
cygkrb5-3.dll
cygkrb5support-0.dll
cygneon-27.dll
cygnettle-4.dll
cygp11-kit-0.dll
cygproxy-1.dll
cygssl-1.0.0.dll
cygstdc++-6.dll
cygtasn1-6.dll
cygwin1.dll
cygz.dll
libgcc_s_dw2-1.dll
libstdc++-6.dll
libwinpthread-1.dll
```

### Download boost https://boost.org
Unpack it to some location, from Windows CMD shell run the .bat script, then run B2 build system:
```
C:
chdir Users\YourName\Downloads\boost-1.61_0
bootstrap.bat
.\b2 toolset=gcc link=shared runtime-link=shared

```

The compiled libraries are in stage\ subdirectory, headers in boost\ .
Copy them to "broot\lib" and "broot\include" respectively.

### Download & compile OpenSSL using MSYS shell
```
cd /c/Users/YourName/Downloads
tar xvf openssl-1.0.2h.tar.gz
cd openssl-1.0.2h
perl Configure shared mingw --prefix=/d/coding/broot
make depend && make && make install
```

### Download & compile libexpat (XML parsing):
Using MSYS shell do this:
```
tar expat-2.2.0.tar.bz2
cd expat-2.2.0
./configure --prefix=/d/coding/broot
make -j4 && make install
```

### Download & compile NEON http://www.webdav.org/neon/
For this one we'll use Cygwin. Run Cygwin's installer and make checks on
everything related to names GCC or MinGW.

Download NEON and place the archive to "D:\cygwin\home\MachineName"

Then copy our "D:\coding\broot" directory to "D:\cygwin\home\MachineName\"
it must contain OpenSSL installation(shared libraries).
Sometimes shared libraries are placed into broot\bin, if the configuration
script can't see it try copying them to broot\lib

Now lets compile libneon: fun in CYGWIN terminal:
```
cd $HOME
tar xvf neon-0.30.1.tar.gz
CFLAGS="-I/usr/include" LDFLAGS="-L/usr/lib" ./configure --prefix=$HOME/broot --with-ssl=openssl
```
Now edit Makefile and replace 1 text's string
``` # Makefile
MAKE=make         #old
MAKE=mingw32-make #new
```
Now compile libneon:
```
make -j4 && make install
```
If install failed, just find and copy "D:\cygwin\home\MachineName\neon-0.30.1\src\.libs\libneon.a" and .h headers to D:\coding\broot\lib and "\include" respectively.

