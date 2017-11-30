/*  Copyright 2017, JP Norair
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
/**
  * @file       fdp_generate.c
  * @author     JP Norair
  * @version    R102
  * @date       25 Nov 2017
  * @brief      Mode 2 File Data Protocol Request Generator Functions
  * @ingroup    ALP's
  *
  ******************************************************************************
  */


#include "hbcc_generate.h"
#include "hbcc_api.h"
#include "hb_asapi.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>





/// Binary Search Table for Commands

#define CMD_NAMESIZE    32
#define CMD_COUNT       12


typedef struct {
	const char     name[CMD_NAMESIZE]; 
	uint32_t       index;
	size_t         argbytes;
	void*          action; 
} fdp_t;


#define ARG_8           1
#define ARG_16          2
#define ARG_UID64       8
#define ARG_SERIAL      0
#define ARG_TOKEN       0
#define ARG_PAYLOAD     0

#define _ISF    3
#define _ISS    2
#define _GFB    1

// sorted list of supported commands
static const hbcc_t commands[CMD_COUNT] = {
    { "del",      _ISF|10,  1,  (void*)&fdp_delete },
    { "del-gfb",  _GFB|10,  1,  (void*)&fdp_delete },
    { "del-isf",  _ISF|10,  1,  (void*)&fdp_delete },
    { "del-iss",  _ISS|10,  1,  (void*)&fdp_delete },
    
    { "new",      _ISF|11,  6,  (void*)&fdp_create },
    { "new-gfb",  _GFB|11,  6,  (void*)&fdp_create },
    { "new-isf",  _ISF|11,  6,  (void*)&fdp_create },
    { "new-iss",  _ISS|11,  6,  (void*)&fdp_create },
    
    { "r",        _ISF|4,   5,  (void*)&fdp_read },
    { "r-gfb",    _GFB|4,   5,  (void*)&fdp_read },
    { "r-isf",    _ISF|4,   5,  (void*)&fdp_read },
    { "r-iss",    _ISS|4,   5,  (void*)&fdp_read },
    
    { "r*",       _ISF|12,  5,  (void*)&fdp_readall },
    { "r*-gfb",   _GFB|12,  5,  (void*)&fdp_readall },
    { "r*-isf",   _ISF|12,  5,  (void*)&fdp_readall },
    { "r*-iss",   _ISS|12,  5,  (void*)&fdp_readall },
    
    { "restore",     _ISF|7, 5, (void*)&fdp_restore },
    { "restore-gfb", _GFB|7, 5, (void*)&fdp_restore },
    { "restore-isf", _ISF|7, 5, (void*)&fdp_restore },
    { "restore-iss", _ISS|7, 5, (void*)&fdp_restore },
    
    { "rh",       _ISF|8,    1,  (void*)&fdp_readhdr },
    { "rh-gfb",   _GFB|8,    1,  (void*)&fdp_readhdr },
    { "rh-isf",   _ISF|8,    1,  (void*)&fdp_readhdr },
    { "rh-iss",   _ISS|8,    1,  (void*)&fdp_readhdr },
    
    { "rp",       _ISF|0,    1,  (void*)&fdp_readperms },
    { "rp-gfb",   _GFB|0,    1,  (void*)&fdp_readperms },
    { "rp-isf",   _ISF|0,    1,  (void*)&fdp_readperms },
    { "rp-iss",   _ISS|0,    1,  (void*)&fdp_readperms },
    
    { "w",        _ISF|7,    5,  (void*)&fdp_write},
    { "w-gfb",    _GFB|7,    5,  (void*)&fdp_write},
    { "w-isf",    _ISF|7,    5,  (void*)&fdp_write},
    { "w-iss",    _ISS|7,    5,  (void*)&fdp_write},
    
    { "wp",       _ISF|3,    2,  (void*)&fdp_writeperms },
    { "wp-gfb",   _GFB|3,    2,  (void*)&fdp_writeperms },
    { "wp-isf",   _ISF|3,    2,  (void*)&fdp_writeperms },
    { "wp-iss",   _ISS|3,    2,  (void*)&fdp_writeperms },
    
    { "z",        _ISF|7,    5,  (void*)&fdp_restore },       //Alias for Restore
    { "z-gfb",    _GFB|7,    5,  (void*)&fdp_restore },
    { "z-isf",    _ISF|7,    5,  (void*)&fdp_restore },
    { "z-iss",    _ISS|7,    5,  (void*)&fdp_restore },
};



// comapres two strings by alphabet,
// returns 0 - if equal, -1 - first one bigger, 1 - 2nd one bigger.
static int local_strcmp(const char *s1, const char *s2);

// comapres first x characters of two strings by alphabet,
// returns 0 - if equal, -1 - first one bigger, 1 - 2nd one bigger.
static int local_strcmpc(const char *s1, const char *s2, int x);

static const hbcc_t* cmd_search(const char *cmdname);



static const hbcc_t* cmd_search(const char *cmdname) {
/// Verify that cmdname is not a zero-length string, then search for it in the
/// list of available commands
    
    if (*cmdname != 0) {
    
        // This is a binary search across the static array
        int l           = 0;
        int r           = CMD_COUNT - 1;
        int cci;
        int csc;
    
        while (r >= l) {
            cci = (l + r) >> 1;
            csc = local_strcmp((char*)commands[cci].name, cmdname);
            
            switch (csc) {
                case -1: r = cci - 1; break;
                case  1: l = cci + 1; break;
                default: return &commands[cci];
            }
        }
        // End of binary search implementation
        
    }
    
	return NULL;
}



static int local_strcmp(const char *s1, const char *s2) {
	for (; (*s1 == *s2) && (*s1 != 0); s1++, s2++);	
	return (*s1 < *s2) - (*s1 > *s2);
}


static int local_strcmpc(const char *s1, const char *s2, int x) {
    for (int i = 0; (*s1 == *s2) && (*s1 != 0) && (i < x - 1); s1++, s2++, i++) ;
    return (*s1 < *s2) - (*s1 > *s2);
}








// Callable processing function
int fdp_generate(uint8_t* dst, size_t* dst_bytes, size_t dstmax, const char* cmd, size_t argbytes, uint8_t* args) {
    const fdp_t* fdp;

    ///@todo update error outputs to not collide with ASAPI errors.
    if ((dst == NULL) || (dst_bytes == NULL) || (cmd == NULL)) {
        return -1;
    }

    fdp = cmd_search(cmd);
    if (hbcc == NULL) {
        return -2;
    }
    if (fdp->index >= CMD_COUNT) {
        return -3;
    }


}





// Return functions are not handled by the server (ignore)
ot_int sub_return(alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in) {
    return 0;
}


ot_bool sub_testchunk(ot_int data_in) {
    return (ot_bool)(data_in > 0);
}



/// This is a form of overwrite protection
ot_bool sub_qnotfull(ot_u8 write, ot_u8 write_size, ot_queue* q) {
    return (ot_bool)(((q->putcursor+write_size) < q->back) || (write == 0));
}




ot_int sub_fileperms( alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in ) {
    ot_int  data_out    = 0;
    vlBLOCK file_block  = (vlBLOCK)((cmd_in >> 4) & 0x07);
    ot_u8   file_mod    = ((cmd_in & 0x02) ? VL_ACCESS_W : VL_ACCESS_R);

    /// Loop through all the listed file ids and process permissions.
    while ((data_in > 0) && sub_qnotfull(respond, 2, alp->outq)) {
        ot_u8   file_id         = q_readbyte(alp->inq);
        ot_bool allow_write     = respond;
        vaddr   header;

        data_in--;  // one for the file id

        if (file_mod == VL_ACCESS_W ) {
            /// run the chmod and return the error code (0 is no error)
            data_in--;  // two for the new mod
            file_mod = vl_chmod(file_block, file_id, q_readbyte(alp->inq), user_id);
        }
        else if (allow_write) {
            /// Get the header address and return mod (offset 5).  The root user
            /// (NULL) is used because this is only for reading filemod.
            /// Note: This is a hack that is extremely optimized for speed
            allow_write = (ot_bool)(vl_getheader_vaddr(&header, file_block, file_id, \
                                                    VL_ACCESS_R, NULL) == 0);
            if (allow_write) {
                ot_uni16 filemod;
                filemod.ushort  = vworm_read(header + 4);   //shortcut to idmod, hack-ish but fast
                file_mod        = filemod.ubyte[1];
            }
        }
        if (allow_write) {
            /// load the data onto the output, if response enabled
            q_writebyte(alp->outq, file_id);
            q_writebyte(alp->outq, file_mod);
            data_out += 2;
        }
    }

    /// return number of bytes put onto the output (always x2)
    //alp->BOOKMARK_IN = (void*)sub_testchunk(data_in);
    return data_out;
}




ot_int sub_fileheaders( alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in ) {
    ot_int  data_out    = 0;
    vlBLOCK file_block  = (vlBLOCK)((cmd_in >> 4) & 0x07);

    /// Only run if respond bit is set!
    if (respond) {
        while ((data_in > 0) && sub_qnotfull(respond, 6, alp->outq)) {
            vaddr   header;
            ot_bool allow_output = True;

            data_in--;  // one for the file id

            allow_output = (ot_bool)(vl_getheader_vaddr(&header, file_block, \
                                    q_readbyte(alp->inq), VL_ACCESS_R, NULL) == 0);
            if (allow_output) {
                q_writeshort_be(alp->outq, vworm_read(header + 4)); // id & mod
                q_writeshort(alp->outq, vworm_read(header + 0)); // length
                q_writeshort(alp->outq, vworm_read(header + 2)); // alloc
                data_out += 6;
            }
        }

        //alp->BOOKMARK_IN = (void*)sub_testchunk(data_in);
    }

    return data_out;
}




ot_int sub_filedata( alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in ) {
    ot_u16  offset;
    ot_u16  span;
    ot_int  data_out    = 0;
    vlFILE* fp          = NULL;
    ot_bool inc_header  = (ot_bool)((cmd_in & 0x0F) == 0x0C);
    vlBLOCK file_block  = (vlBLOCK)((cmd_in >> 4) & 0x07);
    ot_u8   file_mod    = ((cmd_in & 0x02) ? VL_ACCESS_W : VL_ACCESS_R);
    ot_queue*  inq      = alp->inq;
    ot_queue*  outq     = alp->outq;

    sub_filedata_TOP:

    while (data_in > 0) {
        vaddr   header;
        ot_u8   err_code;
        ot_u8   file_id;
        ot_u16  limit;

        //alp->BOOKMARK_IN    = inq->getcursor;
        //alp->BOOKMARK_OUT   = NULL;

        file_id     = q_readbyte(inq);
        offset      = q_readshort(inq);
        span        = q_readshort(inq);
        limit       = offset + span;
        err_code    = vl_getheader_vaddr(&header, file_block, file_id, file_mod, user_id);
        file_mod    = ((file_mod & VL_ACCESS_W) != 0);
        //fp          = NULL;

        // A. File error catcher Stage
        // (In this case, gotos make it more readable)

        /// Make sure file header was retrieved properly, or goto error
        if (err_code != 0) {
            goto sub_filedata_senderror;
        }

        /// Make sure file opens properly, or goto error
        fp = vl_open_file(header);
        if (fp == NULL) {
            err_code = 0xFF;
            goto sub_filedata_senderror;
        }

        /// Make sure offset is within file bounds, or goto error
        if (offset >= fp->alloc) {
            err_code = 0x07;
            goto sub_filedata_senderror;
        }

        if (limit > fp->alloc) {
            limit       = fp->alloc;
            err_code    = 0x08;
        }

        // B. File Writing or Reading Stage
        // Write to file
        // 1. Process error on bad ALP parameters, but still do partial write
        // 2. offset, span are adjusted to convey leftover data
        // 3. miscellaneous write error occurs when vl_write fails
        if (file_mod) {
            for (; offset<limit; offset+=2, span-=2, data_in-=2) {
                if (inq->getcursor >= inq->back) {
                    goto sub_filedata_overrun;
                }
                err_code |= vl_write(fp, offset, q_readshort_be(inq));
            }
        }

        // Read from File
        // 1. No error for bad read parameter, just fix the limit
        // 2. If inc_header param is set, include the file header in output
        // 3. Read out file data
        else {
            ot_u8 overhead;
            //limit       = (limit > fp->length) ? fp->length : limit;
            overhead    = 6;
            overhead   += (inc_header != 0) << 2;

            if ((outq->putcursor+overhead) >= outq->back) {
                goto sub_filedata_overrun;
            }

            q_writeshort_be(outq, vworm_read(header + 4)); // id & mod
            if (inc_header) {
                q_writeshort(outq, vworm_read(header + 0));    // length
                q_writeshort(outq, vworm_read(header + 2));    // alloc
                data_out += 4;
            }
            q_writeshort(outq, offset);
            q_writeshort(outq, span);
            data_out += 6;

            for (; offset<limit; offset+=2, span-=2, data_out+=2) {
                if ((outq->putcursor+2) >= outq->back) {
                    goto sub_filedata_overrun;
                }
                q_writeshort_be(outq, vl_read(fp, offset));
            }
        }

        // C. Error Sending Stage
        sub_filedata_senderror:
        if ((respond != 0) && (err_code | file_mod)) {
            if ((outq->putcursor+2) >= outq->back) {
                goto sub_filedata_overrun;
            }
            q_writebyte(outq, file_id);
            q_writebyte(outq, err_code);
            q_markbyte(inq, span);         // go past any leftover input data
            data_out += 2;
        }

        data_in -= 5;   // 5 bytes input header
        vl_close(fp);
    }


    // Total Completion:
    // Set bookmark to NULL, because the record was completely processed
    //alp->BOOKMARK_IN = NULL;
    return data_out;


    // Partial or Non Completion:
    // Reconfigure last ALP operation, because it was not completely processed

    ///@todo Bookmarking is obsolete, because the way Chunking is done has
    /// been revised.  Chunked records must be contiguous.  ALP-Main will not
    /// call this app, and thus not call this function, until the message-end
    /// bit is detected, therefore meaning that all data is received and
    /// contiguous.  This overrun block, thus, should only check the flags for
    /// chunking, bypass them, and loop back to the top of this function.
    sub_filedata_overrun:
    vl_close(fp);

    ///@todo alp_next_chunk(alp);

//    {
//        ot_u8* scratch;
//        inq->getcursor  = (ot_u8*)alp->BOOKMARK_IN;
//        scratch         = inq->getcursor + 1;
//        *scratch++      = ((ot_u8*)&offset)[UPPER];
//        *scratch++      = ((ot_u8*)&offset)[LOWER];
//        *scratch++      = ((ot_u8*)&span)[UPPER];
//        *scratch        = ((ot_u8*)&span)[LOWER];
//    }

    return data_out;
}






ot_int sub_filedelete( alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in ) {
    ot_int  data_out    = 0;
    vlBLOCK file_block  = (vlBLOCK)((cmd_in >> 4) & 0x07);

    while ((data_in > 0) && sub_qnotfull(respond, 2, alp->outq)) {
        ot_u8   err_code;
        ot_u8   file_id;

        data_in--;
        file_id     = q_readbyte(alp->inq);
        err_code    = vl_delete(file_block, file_id, user_id);

        if (respond) {
            q_writebyte(alp->outq, file_id);
            q_writebyte(alp->outq, err_code);
            data_out += 2;
        }
    }

    //alp->BOOKMARK_IN = (void*)sub_testchunk(data_in);
    return data_out;
}




ot_int sub_filecreate(alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in) {
    ot_int  data_out    = 0;
    vlBLOCK file_block  = (vlBLOCK)((cmd_in >> 4) & 0x07);

    while ((data_in > 0) && sub_qnotfull(respond, 2, alp->outq)) {
        vlFILE*     fp = NULL;
        ot_u8       id;
        ot_u8       mod;
        ot_u16      alloc;
        ot_u8       err_code;

        data_in            -= 6;
        id                  = *alp->inq->getcursor++;
        mod                 = *alp->inq->getcursor;
        alp->inq->getcursor+= 3;                        // cursor goes past mod+length (length ignored)
        alloc               = q_readshort(alp->inq);
        err_code            = vl_new(&fp, file_block, id, mod, alloc, user_id);

        if (respond) {
            q_writebyte(alp->outq, id);
            q_writebyte(alp->outq, err_code);
            data_out += 2;
        }

        vl_close(fp);
    }

    //alp->BOOKMARK_IN = (void*)sub_testchunk(data_in);
    return data_out;
}




/// Not currently supported, always returns "unrestorable" error
ot_int sub_filerestore(alp_tmpl* alp, id_tmpl* user_id, ot_u8 respond, ot_u8 cmd_in, ot_int data_in ) {
    ot_int  data_out    = 0;
    //vlBLOCK file_block  = ((cmd_in >> 4) & 0x07);

    while ((data_in > 0) && sub_qnotfull(respond, 2, alp->outq)) {
        ot_u8   err_code    = 0x03;
        ot_u8   file_id     = q_readbyte(alp->inq);
        data_in            -= 1;

        if (respond) {
            q_writebyte(alp->outq, file_id);
            q_writebyte(alp->outq, err_code);
            data_out += 2;
        }
    }

    //alp->BOOKMARK_IN = (void*)sub_testchunk(data_in);
    return data_out;
}






