import os
import signal
import sys
import binascii

from subprocess import Popen, PIPE, STDOUT
import select

import threading
import time

import json



# Local libraries
sys.path.insert(0, '../pylibs')
from printf import printf
from list_tty import *


# Gateway ID.
# Hard coded for the purpose of this test.
# Can be reconciled via otter as well.
# It will be a 64bit number, represented in Hex
gateway_id = "DA57010203040506"
mqtt_topic_prefix = "haystack"

otter_app    = "./bin/otter"
otter_tty    = "/dev/tty.usbmodem14143"
otter_config = "./otter_cfg.json"

# Number of seconds to wait between MQTT command bounces
bouncer_period_secs = 6









def signal_handler(signal, frame):
        global otter_outthread
        global otter_errthread

        otter_outthread.do_run = False
        otter_errthread.do_run = False
        # sys.exit(0)
        
        
# Thread (used twice) for Sniffing Otter STDERR and STDOUT
def reporter(outfile, filename):
    global otter_pipe

    t = threading.currentThread()
    with outfile:
        for line in iter(outfile.readline, b''):
            printf("%s> %s", filename, line)
            if getattr(t, "do_run", False):
                break



def otter_pipes(pubtopic_dict):
    """
    Reads data from otter pipes and forwards.
    """
    global brokerwait
    brokerwait.acquire()

    if (pubtopic_dict != None):
        fifolist = list()
        topiclist = list()
        otterlist = list()

        for key, value in pubtopic_dict.iteritems():
            otter_filepath = "./" + str(key)
            topic_filepath = "./pub/" + str(value)

            try:
                newfifo = os.open(otter_filepath, os.O_RDONLY|os.O_NONBLOCK)
            except OSError:
                # if file won't open, just move to next and have shorter list
                printf("interlink> otter pub \"%s\" won't open\n", otter_filepath)
                #continue

            printf("interlink> otter pub \"%s\" opened\n", otter_filepath)
            fifolist.append(newfifo)
            topiclist.append(topic_filepath)
            otterlist.append(otter_filepath)

        if (len(fifolist) > 0):
            t = threading.currentThread()

            while getattr(t, "do_run", True):
                indata, outdata, exceptions = select.select(fifolist, [], [], 1)

                for i in range(len(fifolist)):
                    fifo = fifolist[i]
                    if fifo in indata:
                        msg = ""
                        while True:
                            newdata = os.read(fifo, 4096)
                            if (len(newdata) == 0):
                                break
                            msg = msg + str(newdata)

                        if (len(msg) == 0):
                            continue

                        msg = bizlogic_example_pub(otterlist[i], msg)
                        if (msg != None):
                            #print len(msg)
                            printf("interlink> pub %d bytes to %s\n", len(msg), topiclist[i])
                            try:
                                with open(topiclist[i], 'w') as outpipe:
                                    outpipe.write(msg)
                            except OSError:
                                print("interlink> Error: hmqtt pipe not open.")

        #for fifo in fifolist:
        #    os.close(fifo)
    
    
    

# ------- Program startup ---------
if __name__ == '__main__':

    # Preliminary optional step: Delete folder structure of Fifos
    # If a fifo gets stuck open due to bad exit from program, this can cause
    # blocking on reads.
    call("rm -rf pub; rm -rf sub; rm -rf pipes", shell=True)

    # Look for serial ports using list_tty library.
    # It produces a selector menu
    otter_tty  = pick_tty("/dev/tty.usbmodem*")
    if (otter_tty == None):
        print("interlink> No suitable tty was found -- exiting")
        exit(0)
        


    # Startup Otter
    input_cfg  = "--config=" + str(otter_config)
    otter_pipe = Popen([otter_app, otter_tty, "115200", input_cfg, "--pipe"], stdout=PIPE, stdin=PIPE, stderr=PIPE, bufsize=1)

    # Allow sigint (^C) to pass through to children
    signal.signal(signal.SIGINT, signal_handler)
    
    # Open threads (2) that report stdin/out/err from Otter
    otter_outthread = threading.Thread(target=reporter, args=(otter_pipe.stdout, "otter-out"))
    otter_outthread.start()
    otter_errthread = threading.Thread(target=reporter, args=(otter_pipe.stderr, "otter-err"))
    otter_errthread.start()
    
    # Open thread that reports logger pipe from Otter
    log_ubx_thread = threading.Thread(target=otter_pipe, args=(pubtopics_dict,))
    log_ubx_thread.start()

    # --------------------------------

    # Wait for all threads to close
    otter_outthread.join()
    otter_errthread.join()

    otter_pipe.send_signal(signal.SIGINT)
    otter_pipe.wait()

    sys.exit(0)

