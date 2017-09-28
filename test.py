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
    global p
    
    '''
    stdout_fd = p.stdout.fileno()
    stderr_fd = p.stderr.fileno()
    while do_subpipe:
        indata, outdata, exceptions = select.select([stdout_fd, stderr_fd], [], [], 1)
        
        if stdout_fd in indata:
            newdata = os.read(stdout_fd, 1024)
            if len(newdata):
                print '>> ' + newdata,
        elif stderrfd in indata:
            newdata = os.read(stderr_fd, 1024)
            if len(newdata):
                print '*> ' + newdata,
    '''
    
    # readline method
    while p.poll() is None:
        output = p.stdout.readline()
        print '>>' + output,
    
    #p.send_signal(signal.SIGINT)
    
    #wait 1 second.  p.wait fails, as does p.communicate.  Stupid Python.
    time.sleep(1)


def subpipe1():
    global p
    
    while p.poll() is None:
        output = p.stderr.readline()
        print '*>' + output,

    time.sleep(1)
    
    
    

# ------- Program startup ---------

sequence1 = "hbcc session_init [01 11 00 88]\n" \
            "hbcc new_adv [40 0400]\n" \
            "hbcc new_inventory [40 0100 0000]\n" \
            "hbcc collect_file_on_token [40 0C F0 0A 3C 2000 0000] \"new-loc-data\"\n" \
            "hbcc collect_auth_on_serial [00 08 20 04 00] \"serial#1\""


do_subpipe = True;

# Allow sigquit to pass through to child
signal.signal(signal.SIGINT, signal_handler)

# The "/dev/tty..." file is hard coded and can be changed.
# It should be made into an input parameter.
p = Popen(['./debug/otter', '/dev/tty.usbmodem14143', '115200', '--config=./input.json', '--pipe'], stdout=PIPE, stdin=PIPE, stderr=PIPE, bufsize=1)

subpipe_thread = threading.Thread(target=subpipe)
subpipe_thread.start()

subpipe1_thread = threading.Thread(target=subpipe1)
subpipe1_thread.start()

# Wait a few seconds
time.sleep(1)

pin = p.stdin.fileno()
print("Opened Fifo %d for writing" % pin)
os.write(pin, "whoami")
time.sleep(1)
#os.close(pin)

#p.stdin.write("raw \"test test test\"\n")
os.write(pin, sequence1)
time.sleep(1)

#p.stdin.write("quit \n")
os.write(pin, "quit")
time.sleep(1)


subpipe_thread.join()
subpipe1_thread.join()

sys.exit(0)

