Locksmith
======================
Locksmith is a library for debugging locking in C, C++, or Objective C programs.

It's designed to catch common locking errors at runtime that might otherwise
lead to deadlocks or crashes.  Locksmith is built on top of pthreads.

How do I build the source code?
----------------------------------
    ./configure
    make
    sudo make install

How to use Locksmith
--------------------------
Using locksmith is simple.  You do not need to recompile your program.  Just
run your program with the LD\_PRELOAD environment variable set to the locksmith
library.  For example,

    LKSMITH_OUTPUT=syslog LD_PRELOAD=/path/to/liblksmith.so /bin/ls

What kinds of errors does Locksmith catch?
--------------------------------------------
1. Locking inversions.
For example, if one thread locks mutex A and then tries to lock mutex B, and
another thread locks mutex B and then tries to locks mutex A.

2. Freeing a mutex or spinlock that you currently hold.
In the pthreads library, freeing a mutex or spinlock that you currently hold
can cause undefined behavior.  You must release it first. Locksmith issues an
error message in this case.

3. Unlocking a mutex from a different thread than the one which locked it.
This is another scenario which triggers undefined behavior in pthreads, but
which Locksmith turns into a hard error.

What choices are available for LKSMITH\_OUTPUT? 
-------------------------------------------------
    LKSMITH_OUTPUT=syslog
This will redirect all output to syslog.

    LKSMITH_OUTPUT=stderr
This will redirect all output to standard error.

    LKSMITH_OUTPUT=stdout
This will redirect all output to standard output.

    LKSMITH_OUTPUT=file:///tmp/foo
This will redirect all output to /tmp/foo.  Substitute your own file name as appropriate.

What languages and libraries is Locksmith compatible with? 
-------------------------------------------------------------
Locksmith should be compatible with every library built on top of pthreads in C
or C++.  This includes boost::mutex, KDE's QMutex, glib's Glib::Mutex, and so
forth.  Locksmith has no problems with global constructors or destructors in
C++.  Locksmith also works with mutexes that have been initialized statically
with PTHREAD\_MUTEX\_INITIALIZER.  Finally, Locksmith handles pthreads mutexes
created and used in a shared library independent of the main executable.

What license is Locksmith under?
-------------------------------------------------------------
Locksmith is released under the 2-clause BSD license.  See LICENSE.txt for
details.

TODO
-------------------------------------------------------------
* warn about taking a sleeping lock when holding a spin lock
* Support pthread rwlocks
* Support pthread barriers
* Add the ability to dump out debugging information about the state of all locks on command.
* Support thread cancellation (?)
* Support POSIX semaphores
* Add a way to suppress deadlock warnings through the use of compile-time annotations.
* Add the ability to name mutexes and threads through the use of compile-time annotations.
* Better support for debugging cross-process mutexes and spin-locks (perhaps by putting Locksmith globals into a shared memory segment?)  This is tricky because cross-process locks won't have the same memory address in different processes.

Contact information
-------------------------------------------------------------
Colin Patrick McCabe <cmccabe@alumni.cmu.edu>
