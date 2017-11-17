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

/// Default feature configurations
#define OTTER_FEATURE(VAL)          OTTER_FEATURE_##VAL
#define OTTER_FEATURE_MPIPE         ENABLED
#define OTTER_FEATURE_MODBUS        DISABLED


/// Parameter configurations
///@todo redefine 
#define OTTER_PARAM(VAL)            OTTER_PARAM_##VAL
#define OTTER_PARAM_VERSION         "0.2.0"
#define OTTER_PARAM_DATE            __DATE__
#define OTTER_PARAM_DEFBAUDRATE     115200



/// Automatic Checks
#if ((OTTER_FEATURE_MPIPE != ENABLED) && (OTTER_FEATURE_MODBUS != ENABLED))
#   error "No TTY interface enabled.  MPipe (default) and Modbus both disabled"
#endif

#if (OTTER_FEATURE_MPIPE != ENABLED)
#   warning "MPipe interface not enabled.  Functionality is not guaranteed."
#endif

#endif
