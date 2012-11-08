#!/usr/bin/python

import copy
import threading
import time


class LMutex(object):
    def __init__(self, name):
        self.mutex = threading.Lock()
        self.selfmutex = threading.Lock()
        self.name = name
        self.before = dict()
        self.after = dict()
    def lock(self, tls):
        after_copy = dict()
        self.selfmutex.acquire()
        for l in self.after.keys():
            after_copy[l] = 1
        self.selfmutex.release()
        add_to_before = dict()

        for l in tls.held.keys():
            if (after_copy.has_key(l)):
                print "lock order inversion! " + str(l),
                print " is supposed to be taken before " + str(self),
                print "."
            else:
                add_to_before[l] = 1
                l.selfmutex.acquire()
                l.after[self] = 1
                l.selfmutex.release()
        self.mutex.acquire()
        self.selfmutex.acquire()
        for l in add_to_before.keys():
            self.after[l] = 1
        self.selfmutex.release()
        tls.held[self] = 1
        tls.print_held_locks()
    def unlock(self, tls):
        del tls.held[self]
        self.mutex.release()
    def __str__(self):
        return self.name

lock_a = LMutex("a")
lock_b = LMutex("b")

class LThreadData(object):
    def __init__(self, ident):
        self.ident = ident
        self.held = dict()
    def __str__(self):
        return "thread " + str(self.ident)
    def print_held_locks(self):
        print "Thread " + str(self.ident) + " now holds locks ",
        for k in sorted(self.held.keys()):
            print str(k) + " ", 
        print
def MyThread1(ident):
    for i in range(1, 100):
        lock_a.lock(ident)
        print str(ident) + " is accessing a!"
        lock_b.lock(ident)
        print str(ident) + " is accessing b!"
        lock_b.unlock(ident)
        lock_a.unlock(ident)
        time.sleep(0.01)
def MyThread2(ident):
    for i in range(1, 100):
        lock_b.lock(ident)
        print str(ident) + " is accessing b!"
        lock_a.lock(ident)
        print str(ident) + " is accessing a!"
        lock_a.unlock(ident)
        lock_b.unlock(ident)
        time.sleep(0.01)

t1 = threading.Thread(target=MyThread1, args=[LThreadData(1)])
t2 = threading.Thread(target=MyThread1, args=[LThreadData(2)])
t3 = threading.Thread(target=MyThread2, args=[LThreadData(3)])
t1.start()
t2.start()
t3.start()
