import os
import signal
import sys
import binascii

from subprocess import Popen, PIPE, STDOUT
import select

import threading
import time



def signal_handler(signal, frame):
    global do_subpipe
    
    do_subpipe = False
    #sys.exit(0)

def bindata_reader(data):
    items = data.splitlines()
    
    print(items[0])

    if ((len(items[1]) & 1) == 0):
        print(items[1].decode("hex"))
    else:
        print("invalid length payload")


def subpipe():
    global do_subpipe
    global p
    
    while do_subpipe:
        indata, outdata, exceptions = select.select([p.stdout, p.stderr], [], [], 1)
        
        if p.stdout in indata:
            newdata = os.read(p.stdout, 1024)
            if len(newdata):
                print '>> ' + newdata,
        elif p.stderr in indata:
            newdata = os.read(p.stderr, 1024)
            if len(newdata):
                print '*> ' + newdata,
    
    p.send_signal(signal.SIGINT)
    p.wait()

    

# ------- Program startup ---------

do_subpipe = True;

# Allow sigquit to pass through to child
signal.signal(signal.SIGINT, signal_handler)

p = Popen("./debug/otter /dev/tty.usbmodem1413 115200 --config=./input.json", stdout=PIPE, stdin=PIPE, stderr=PIPE, bufsize=1)


subpipe_thread = threading.Thread(target=subpipe)
subpipe_thread.start()

# Wait a few seconds
time.sleep(3)

# Dopey testing of a few input strings
pout, _ = p.communicate("whoami")
print pout
time.sleep(3)

pout, _ = p.communicate("whoami")
print pout
time.sleep(3)

time.sleep(3)


subpipe_thread.join()


sys.exit(0)

