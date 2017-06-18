/* Copyright 2017, JP Norair
  *
  * Licensed under the OpenTag License, Version 1.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License at
  *
  * http://www.indigresso.com/wiki/doku.php?id=opentag:license_1_0
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  */

#include "ppipe.h"


int ppipe_open(char* call, const char* mode, size_t rbuf_size, size_t wbuf_size) {
    FILE *pipe_fp;
    int pipe_fd = -1;

    if (call != NULL) {

        /// Open pipe to external call.
        /// @note on mac this can be bidirectional, on linux it will need to be
        /// rewritten with popen() and a separate input pipe.
        pipe_fp = popen(external_call, mode);
        //fcntl(pipe_fp, F_SETFL, O_NONBLOCK);
        
        if (pipe_fp != NULL) {
            // allocate buffers
            
            // increment fd value
            
            // spawn rx thread?
        }
    }
    
    return pipe_fd;
}


int ppipe_close(int pipe_fd) {
    // 1. close the pipe with pclose()
    // 2. terminate rx thread, if exists
    // 3. clear the line in the table
    // 4. free the read and write buffers

    return 0;
}



int ppipe_send(int pipe_fd, uint8_t* data, size_t data_len) {
    
    return 0;
}



int ppipe_reader() {
    // This is a reader thread
    // It should output to the mpipe output stream
    // All the data formatters in mpipe.c should be pulled into separate C file.
    
    
    
    return 0;
}
