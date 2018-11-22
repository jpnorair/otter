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
#define _E_NRM  "\033[0m"
#define _E_BLK  "\033[30;40m"
#define _E_RED  "\033[31;40m"
#define _E_GRN  "\033[32;40m"
#define _E_YEL  "\033[33;40m"
#define _E_BLU  "\033[34;40m"
#define _E_MAG  "\033[35;40m"
#define _E_CYN  "\033[36;40m"
#define _E_WHT  "\033[37;40m"

#define _E_BBLK "\033[1;30;40m"
#define _E_BRED "\033[1;31;40m"
#define _E_BGRN "\033[1;32;40m"
#define _E_BYEL "\033[1;33;40m"
#define _E_BBLU "\033[1;34;40m"
#define _E_BMAG "\033[1;35;40m"
#define _E_BCYN "\033[1;36;40m"
#define _E_BWHT "\033[1;37;40m"


/// Default feature configurations
#define OTTER_FEATURE(VAL)          OTTER_FEATURE_##VAL
#ifndef OTTER_FEATURE_MPIPE
#   define OTTER_FEATURE_MPIPE      ENABLED
#endif
#ifndef OTTER_FEATURE_MODBUS
#   define OTTER_FEATURE_MODBUS     DISABLED
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
#ifndef OTTER_FEATURE_OTDB
#   define OTTER_FEATURE_OTDB       DISABLED
#endif

/// Parameter configurations
///@todo redefine 
#define OTTER_PARAM(VAL)            OTTER_PARAM_##VAL
#ifndef OTTER_PARAM_NAME
#   define OTTER_PARAM_NAME         "otter"
#endif
#ifndef OTTER_PARAM_VERSION 
#   define OTTER_PARAM_VERSION      "0.3.0"
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
#ifndef OTTER_PARAM_MBTIMEOUT
#   define OTTER_PARAM_MBTIMEOUT    20
#endif
#ifndef OTTER_PARAM_ENCALIGN
#   define OTTER_PARAM_ENCALIGN     1
#endif


/// Automatic Checks
#if ((OTTER_FEATURE_MPIPE != ENABLED) && (OTTER_FEATURE_MODBUS != ENABLED))
#   error "No TTY interface enabled.  MPipe (default) and Modbus both disabled"
#endif

#if (OTTER_FEATURE_MPIPE != ENABLED)
#   warning "MPipe interface not enabled.  Functionality is not guaranteed."
#endif

#if !((OTTER_PARAM_ENCALIGN == 1) || (OTTER_PARAM_ENCALIGN == 2) || (OTTER_PARAM_ENCALIGN == 4))
#   error "OTTER_PARAM_ENCALIGN must be 1, 2, or 4.  Default=1"
#endif

#endif
