/* Copyright 2014, JP Norair
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

#ifndef otter_cfg_h
#define otter_cfg_h

#ifndef ENABLED
#   define ENABLED  1
#endif
#ifndef DISABLED
#   define DISABLED 0
#endif


/// Color codes for terminal output coloring
///@todo move this and printer macros to a debug.h

#define _E_NRM  "\033[0m"

// Normal colors
#define _E_RED  "\033[31m"
#define _E_GRN  "\033[32m"
#define _E_YEL  "\033[33m"
#define _E_BLU  "\033[34m"
#define _E_MAG  "\033[35m"
#define _E_CYN  "\033[36m"
#define _E_WHT  "\033[37m"

// Overlay color on black
#define _E_OBLK  "\033[30;40m"
#define _E_ORED  "\033[31;40m"
#define _E_OGRN  "\033[32;40m"
#define _E_OYEL  "\033[33;40m"
#define _E_OBLU  "\033[34;40m"
#define _E_OMAG  "\033[35;40m"
#define _E_OCYN  "\033[36;40m"
#define _E_OWHT  "\033[37;40m"

// Bright
#define _E_BBLK "\033[1;30;40m"
#define _E_BRED "\033[1;31;40m"
#define _E_BGRN "\033[1;32;40m"
#define _E_BYEL "\033[1;33;40m"
#define _E_BBLU "\033[1;34;40m"
#define _E_BMAG "\033[1;35;40m"
#define _E_BCYN "\033[1;36;40m"
#define _E_BWHT "\033[1;37;40m"

#define ERRMARK                 _E_RED"ERR: "_E_NRM
#define ERR_PRINTF(...)         do { if (cliopt_isverbose()) { fprintf(stdout, _E_RED "ERR: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)
#define VERBOSE_PRINTF(...)     do { if (cliopt_isverbose()) { fprintf(stdout, _E_CYN "MSG: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)
#define VDATA_PRINTF(...)       do { if (cliopt_isverbose()) { fprintf(stdout, _E_GRN "DATA: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)
#define VCLIENT_PRINTF(...)     do { if (cliopt_isverbose()) { fprintf(stdout, _E_MAG "CLIENT: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)







/// Default feature configurations
#define OTTER_FEATURE(VAL)          OTTER_FEATURE_##VAL
#ifndef OTTER_FEATURE_MPIPE
#   define OTTER_FEATURE_MPIPE      ENABLED
#endif
#ifndef OTTER_FEATURE_MODBUS
#   define OTTER_FEATURE_MODBUS     ENABLED
#endif

#ifndef OTTER_FEATURE_RUBBISHTTY
#   define OTTER_FEATURE_RUBBISHTTY DISABLED
#endif
#ifndef OTTER_FEATURE_NOPOLL
#   define OTTER_FEATURE_NOPOLL     (DISABLED || OTTER_FEATURE_RUBBISHTTY)
#endif
#ifndef OTTER_FEATURE_NOFLUSH
#   define OTTER_FEATURE_NOFLUSH    (DISABLED || OTTER_FEATURE_RUBBISHTTY)
#endif
#ifndef OTTER_FEATURE_MANDRAIN
#   define OTTER_FEATURE_MANDRAIN   (DISABLED || OTTER_FEATURE_RUBBISHTTY)
#endif

#ifndef OTTER_FEATURE_HBUILDER
#   ifdef __HBUILDER__
#   define OTTER_FEATURE_HBUILDER   ENABLED
#   else
#   define OTTER_FEATURE_HBUILDER   DISABLED
#   endif
#endif
#ifndef OTTER_FEATURE_SECURITY
#   define OTTER_FEATURE_SECURITY   (DISABLED || OTTER_FEATURE_HBUILDER)
#endif


/// Parameter configuration defaults
///@todo some of these should be otter environment variables, namely timeouts and addresses
#define OTTER_PARAM(VAL)            OTTER_PARAM_##VAL
#ifndef OTTER_PARAM_NAME
#   define OTTER_PARAM_NAME         "otter"
#endif
#ifndef OTTER_PARAM_VERSION 
#   define OTTER_PARAM_VERSION      "1.0.a"
#endif
#ifndef OTTER_PARAM_GITHEAD
#   define OTTER_PARAM_GITHEAD      "(unknown)"
#endif
#ifndef OTTER_PARAM_DATE
#   define OTTER_PARAM_DATE         __DATE__
#endif
#ifndef OTTER_PARAM_BYLINE
#   define OTTER_PARAM_BYLINE       "Haystack Technologies, Inc."
#endif
#ifndef OTTER_PARAM_DEFBAUDRATE
#   define OTTER_PARAM_DEFBAUDRATE  115200
#endif
#ifndef OTTER_PARAM_MMAP_PAGESIZE
#   define OTTER_PARAM_MMAP_PAGESIZE (128*1024)
#endif
#ifndef OTTER_PARAM_MBTIMEOUT
#   define OTTER_PARAM_MBTIMEOUT    20
#endif
#ifndef OTTER_PARAM_DEFMBSLAVE
#   define OTTER_PARAM_DEFMBSLAVE   2
#endif
#ifndef OTTER_PARAM_ENCALIGN
#   define OTTER_PARAM_ENCALIGN     1
#endif
#ifndef OTTER_DEVTAB_CHUNK
#   define OTTER_DEVTAB_CHUNK       1
#endif
#ifndef OTTER_SUBSCR_CHUNK
#   define OTTER_SUBSCR_CHUNK       3
#endif

/// Automatic Checkss
#if ((OTTER_FEATURE_MPIPE != ENABLED) && (OTTER_FEATURE_MODBUS != ENABLED))
#   error "No TTY interface enabled.  MPipe (default) and Modbus both disabled"
#endif

//#if (OTTER_FEATURE_MPIPE != ENABLED)
//#   warning "MPipe interface not enabled.  Functionality is not guaranteed."
//#endif

#if !((OTTER_PARAM_ENCALIGN == 1) || (OTTER_PARAM_ENCALIGN == 2) || (OTTER_PARAM_ENCALIGN == 4))
#   error "OTTER_PARAM_ENCALIGN must be 1, 2, or 4.  Default=1"
#endif






#endif
