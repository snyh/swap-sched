#!/usr/bin/python

from bcc import BPF
from time import sleep
from pathlib2 import Path
import ctypes as ct
import psutil
import sys
import os

b = BPF(src_file="tracer.c")

KB = 1024
MB = 1024 * KB

US = 1000
MS = 1000 * US
S = 1000 * MS

mypid = os.getpid()


def humtime(s):
    if s > S:
        return "%ds" % (s / S)
    elif s > MS:
        return "%dms" % (s / MS)
    elif s > US:
        return "%dus" % (s / US)
    else:
        return "%dns" % (s)


def humsize(s):
    if s > MB:
        return "%dMB" % (s / MB)
    elif s > KB:
        return "%dKB" % (s / KB)
    else:
        return "%d" % s

while True:
    sleep(1)
    os.system("clear")
    print("  %-40s\t%s %s %s %s %s" %
          ("COMMAND", "PageFault", "USS", "SWAP", "SYSCALL", "Involuntary"))
    items = sorted(b["pfs"].items(), reverse=True, key=lambda t: t[1].value)
    dsleeps = b["sleeps"]
    for k, v in items[:10]:
        if k.pid == mypid:
            continue
        comm = ""
        try:
            p = psutil.Process(k.pid)
            comm = p.exe()
            minfo = p.memory_full_info()
            print(
                "%u %-40s \t %s\t%s\t%s\t%s\t%s" %
                (k.pid,
                 comm[:40],
                 humtime(v.value),
                 humsize(minfo.uss), humsize(minfo.swap),
                 humtime(dsleeps[k].value),
                 p.num_ctx_switches().involuntary,
                 ))
        except Exception as e:
            print (e)
            pass
