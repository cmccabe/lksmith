#!/usr/bin/python

import threading
import time


class LMutex(object):
    def __init__(self, name):
        self.mutex = threading.Lock()
        self.name = name
    def lock(self):
        self.mutex.acquire()
    def unlock(self):
        self.mutex.release()

lock_a = LMutex("a")
lock_b = LMutex("b")

class LThreadData(object):
    def __init__(self, ident):
        self.ident = ident
    def __str__(self):
        return "thread " + str(self.ident)

def MyThread1(id):
    for i in range(1, 100):
        lock_a.lock()
        print str(id) + " is accessing a!"
        lock_a.unlock()
        time.sleep(0.01)
    pass
def MyThread2():
    print "thread 2!"
    pass

t1 = threading.Thread(target=MyThread1, args=[LThreadData(1)])
t2 = threading.Thread(target=MyThread1, args=[LThreadData(2)])
t3 = threading.Thread(target=MyThread1, args=[LThreadData(3)])
t1.start()
t2.start()
t3.start()
