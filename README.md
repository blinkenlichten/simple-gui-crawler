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
+ Install openSSL(development) and boost libraries (modules: system, regex, thread, atomic) into system path or
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
It got a little bit tricky, because of using MinGW compiler and libneon which is primarly created for Linux, it is based on ./configure BASH script that makes the project.
So, you need these steps to create the build environment:

### Make build destination directory
Example: D:\coding\broot  or /d/coding/broot in MSYS/Cygwin shell.

### MinGW
Install and add location where mingw-gcc.exe lays to PATH system variable,
for example C:\MinGW\bin

### Cygwin
Install to D:\cygwin for example

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

Download NEON and place the archive to D:\cygwin\home\MachineName

Then copy our D:\coding\broot directory to D:\cygwin\home\MachineName\
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

### Qt project file
Now copy all contents of D:\coding\broot to project's subdir test03-v03\3rdparty.
Edit test03-v03.pro file so that it points to correct boost libraries paths and names, for example 
```
# MinGW32 only, at the moment
libpath = $$_PRO_FILE_PWD_/3rdparty/lib/

#modify for your version:
endian = "-mgw49-mt-1_61.dll.a"

```
Compile the project using QMake or QtCreator IDE.

### Deployment on Windows: OpenSSL and Qt5
Due to different licensing approach, Qt5 does not link to OpenSSL libraries,
instead they load the library at runtime at the path where program.exe is located.
Copy these to the executable's directory: libeay32.dll ssleay32.dll libgcc_s_dw2.dll


# Networking: 2 approches
As can seen from the repo's history the initial idea was to use boost.asio and it's SSL socket, but it went wrong with HTTPS, it needs more tedious work.
So I threw it out and decided to use some kind of C library with straightforward API that will just let you to do the jobs.

# C-API library
My considerations were NEON (http & webdav client) and CURL.
NEON has much simpler API, because it's intended for HTTP(S) only whereas CURL works over almost any protocol and hence has more complex API.
Both of them have UNIX-style configurations system and require to use either MSYS or Cygwin (worked out for neon).

# Qt5: QNetworkAccessManager
The program also has use case of Qt5 networking API, but it somehow doesn't call the documented API signals, might be a Qt's bug or wrong use case.
It can be turned on on by .pro file variable "VAR_NO_LIBNEON=1",
then libneon won't be needed ... but the program will fail to download the pages.
# Others: Microsoft's Cassandra (client/server), Poco libraries
They're nice, but I couldn't afford MSVC to compile them, it always goes wrong with Microsoft's mutilated free version of MSVC. Thus I had to play around MinGW.


# Status
The algorithm is working with minimal use of mutex locking, it needs testing.

# Ideas
The program has reduced to minimum use of explicitly defined sync. variables: mutexex or spinlocks,
we're syncronizing the tasks either using RAII ownership passing,
volatile bool flags and or higher
level task management entities: boost::base_threadpool (waits on condition),
Qt5 events loop in main thread (same thing).
They're using mutex lock + condition wait and it's just fine.
All other entities do not block when resources have to be passed between threads,
they pass things encapsulated in std::shared_ptr and functors.

I know the drawbacks of using functors everywhere(too much context switching),
but at least I'm not writing this stuff in JavaScript/PHP with consequent asking of the
employer to buy new mega-computer-server-dozer2000 to make that shit work :-D

## Regular expressions
To extract http:// I'm using boost::regex (C++11 has std::regex with same templated API).
The set of expressions is defined in "webgrep/crawler_worker.h" in the class WorkerCtx constructor.
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


List of current runtime dependencies for build under MS Windows & Cygwin,
the item cygneon-27.dll is the one that has been compiled under Cygwin manually.

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
libboost_atomic-mgw49-mt-1_61.dll
libboost_container-mgw49-mt-1_61.dll
libboost_context-mgw49-mt-1_61.dll
libboost_coroutine-mgw49-mt-1_61.dll
libboost_system-mgw49-mt-1_61.dll
libboost_thread-mgw49-mt-1_61.dll
libgcc_s_dw2-1.dll
libneon-27.dll
libstdc++-6.dll
libwinpthread-1.dll
Qt5Cored.dll
Qt5Guid.dll
Qt5Widgetsd.dll
```
