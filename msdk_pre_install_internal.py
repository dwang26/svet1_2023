#!/usr/bin/python
"""

Copyright (c) 2018 Intel Corporation All Rights Reserved.

THESE MATERIALS ARE PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THESE
MATERIALS, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

File Name: msdk_pre_install.py

Abstract: Complete install for Intel Visual Analytics, including
 * Intel(R) Media SDK
 * Intel(R) Media driver
 * Libva
 * Drivers
 * Prerequisites
"""

import os, sys, platform
import os.path
import argparse
import subprocess
import grp

class diagnostic_colors:
    ERROR   = '\x1b[31;1m'  # Red/bold
    SUCCESS = '\x1b[32;1m'  # green/bold
    RESET   = '\x1b[0m'     # Reset attributes
    INFO    = '\x1b[34;1m'  # info
    OUTPUT  = ''            # command's coutput printing
    STDERR  = '\x1b[36;1m'  # cyan/bold
    SKIPPED = '\x1b[33;1m'  # yellow/bold

class loglevelcode:
    ERROR   = 0
    SUCCESS = 1
    INFO    = 2

GLOBAL_LOGLEVEL=3

def print_info( msg, loglevel ):
    global GLOBAL_LOGLEVEL

    """ printing information """

    if loglevel==loglevelcode.ERROR and GLOBAL_LOGLEVEL>=0:
        color = diagnostic_colors.ERROR
        msgtype=" [ ERROR ] "
        print( color + msgtype + diagnostic_colors.RESET + msg )
    elif loglevel==loglevelcode.SUCCESS and GLOBAL_LOGLEVEL>=1:
        color = diagnostic_colors.SUCCESS
        msgtype=" [ OK ] "
        print( color + msgtype + diagnostic_colors.RESET + msg )
    elif loglevel==loglevelcode.INFO and GLOBAL_LOGLEVEL>=2:
        color = diagnostic_colors.INFO
        msgtype=" [ INFO ] "
        print( color + msgtype + diagnostic_colors.RESET + msg )
    return

def run_cmd(cmd):
    output=""
    fin=os.popen(cmd+" 2>&1","r")
    for line in fin:
        output+=line
    fin.close()
    return output

def fnParseCommandline():
    if len(sys.argv) == 1:
        return "-all"
    elif len(sys.argv) > 3:
        return "not support"
        exit(0)

    if sys.argv[1] == "-h":
        print("[%s" % sys.argv[0] + " usage]")
        print( "\t -h: display help")
        print( "\t -all: install all components")
        print( "\t -b BUILD_TARGET: build all components")
        print( "\t -m : build msdk only")
        exit(0)

    return sys.argv[1]

if __name__ == "__main__":

    LIBVA_INSTALL_PREFIX="/opt/intel/svet/msdk"
    LIBVA_INSTALL_PATH=LIBVA_INSTALL_PREFIX+"/lib"
    LIBVA_DRIVERS_PATH=LIBVA_INSTALL_PATH+"/dri"
    LIBVA_DRIVER_NAME="iHD"

    WORKING_DIR=run_cmd("pwd").strip()
    msg_tmp = "Working directory: " + WORKING_DIR
    print_info(msg_tmp, loglevelcode.INFO)

    BUILD_TARGET=""
    build_msdk = False
    build_all = False

    cmd = fnParseCommandline()

    if cmd == "-b":
        BUILD_TARGET=sys.argv[2]
        build_all = True
        pre_install = False
    elif cmd == "-m":
        print_info("Build MSDK", loglevelcode.INFO)
        build_msdk = True
        pre_install = False
    else:
        pre_install = True


    if pre_install == True:
        print("")
        print("************************************************************************")
        print_info("Install required tools and create build environment.", loglevelcode.INFO)
        print("************************************************************************")

        # Install the necessary tools

        print("Please input the sudo password to proceed\n")
        cmd ="sudo apt-get -y install git libssl-dev dh-autoreconf cmake libgl1-mesa-dev libpciaccess-dev build-essential curl unzip-dev libavcodec-dev libavutil-dev libavformat-dev;"
        cmd+="sudo apt-get -y install coreutils pkg-config opencl-clhpp-headers opencl-c-headers ocl-icd-opencl-dev"
        os.system(cmd)

        print("")
        print("************************************************************************")
        print_info("Pull all the source code.", loglevelcode.INFO)
        print("************************************************************************")

        # Pull all the source code
        print("libva")
        if not os.path.exists("%s/libva"%(WORKING_DIR)):
            cmd = "cd %s; git clone https://github.com/intel/libva.git;"%(WORKING_DIR)
            cmd+= "cd libva;"
            cmd+="git checkout 2.11.0"
            print(cmd)
            os.system(cmd);

        print("libva-utils")
        if not os.path.exists("%s/libva-utils"%(WORKING_DIR)):
            cmd = "cd %s; git clone https://github.com/intel/libva-utils.git;"%(WORKING_DIR)
            cmd += "cd libva-utils;"
            cmd+="git checkout 2.11.1"
            print(cmd)
            os.system(cmd);

        print("media-driver")
        if not os.path.exists("%s/media-driver"%(WORKING_DIR)):
            cmd = "cd %s; git clone https://github.com/intel/media-driver.git; "%(WORKING_DIR)
            cmd += "cd media-driver;"
            cmd+= "git checkout intel-media-21.1.3"
            print(cmd)
            os.system(cmd);

        print("gmmlib")
        if not os.path.exists("%s/gmmlib"%(WORKING_DIR)):
            cmd = "cd %s; git clone https://github.com/intel/gmmlib.git; "%(WORKING_DIR)
            cmd += "cd gmmlib;"
            cmd+= "git checkout intel-gmmlib-21.1.1"
            print(cmd)
            os.system(cmd);

        print("MediaSDK")
        if not os.path.exists("%s/MediaSDK"%(WORKING_DIR)):
            cmd = "cd %s; git clone https://github.com/Intel-Media-SDK/MediaSDK.git; "%(WORKING_DIR)
            cmd+= "cd MediaSDK;"
            cmd+= "git checkout intel-mediasdk-21.1.3"
            print(cmd)
            os.system(cmd);


    if build_all == True:
        print("")
        print("************************************************************************")
        print_info("Build and Install libVA", loglevelcode.INFO)
        print("************************************************************************")

        # Build and install libVA including the libVA utils for vainfo.
        # libVA origin:fbf7138389f7d6adb6ca743d0ddf2dbc232895f6 (011118), libVA utils origin: 7b85ff442d99c233fb901a6fe3407d5308971645 (011118)
        cmd ="export LIBRARY_PATH=%s:$LIBRARY_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="cd %s/libva; "%(WORKING_DIR)
        cmd+="./autogen.sh --prefix=%s --libdir=%s; make -j4; sudo make install"%(LIBVA_INSTALL_PREFIX, LIBVA_INSTALL_PATH)
        print(cmd)
        os.system(cmd)

        cmd ="export PKG_CONFIG_PATH=%s/pkgconfig:$PKG_CONFIG_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="export LIBRARY_PATH=%s:$LIBRARY_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="export C_INCLUDE_PATH=%s/include:$C_INCLUDE_PATH; "%(LIBVA_INSTALL_PREFIX)
        cmd+="export CPLUS_INCLUDE_PATH=%s/include:$CPLUS_INCLUDE_PATH; "%(LIBVA_INSTALL_PREFIX)
        cmd+="cd %s/libva-utils; "%(WORKING_DIR)
        cmd+="./autogen.sh --prefix=%s --libdir=%s; make -j4; sudo make install"%(LIBVA_INSTALL_PREFIX, LIBVA_INSTALL_PATH)
        print(cmd)
        os.system(cmd)

        print("")
        print("************************************************************************")
        print_info("Build and Install media driver", loglevelcode.INFO)
        print("************************************************************************")

        # Build and install media driver
        cmd ="export LIBRARY_PATH=%s:$LIBRARY_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+= "rm -rf %s/gmmlib/build; "%(WORKING_DIR)
        cmd+= "mkdir -p %s/gmmlib/build; "%(WORKING_DIR)
        cmd+= "cd %s/gmmlib/build; "%(WORKING_DIR)
        cmd+= "cmake ../ -DCMAKE_INSTALL_PREFIX=%s ;"%(LIBVA_INSTALL_PREFIX)
        cmd+= "make -j4;"
        cmd+= "sudo make install;"
        print(cmd)
        os.system(cmd)

        cmd ="export PKG_CONFIG_PATH=%s/pkgconfig:$PKG_CONFIG_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="export LD_LIBRARY_PATH=%s:$LD_LIBRARY_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="export LIBRARY_PATH=%s:$LIBRARY_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="export C_INCLUDE_PATH=%s/include:$C_INCLUDE_PATH; "%(LIBVA_INSTALL_PREFIX)
        cmd+="export CPLUS_INCLUDE_PATH=%s/include:$CPLUS_INCLUDE_PATH; "%(LIBVA_INSTALL_PREFIX)
        cmd+= "rm -rf %s/media_build; "%(WORKING_DIR)
        cmd+= "mkdir -p %s/media_build; "%(WORKING_DIR)
        cmd+= "cd %s/media_build; "%(WORKING_DIR)
        cmd+= "cmake ../media-driver -DCMAKE_INSTALL_PREFIX=%s -DLIBVA_INSTALL_PATH=%s; "%(LIBVA_INSTALL_PREFIX, LIBVA_INSTALL_PATH)
        cmd+= "make -j4; "
        cmd+= "sudo LD_LIBRARY_PATH=%s:$LD_LIBRARY_PATH LIBRARY_PATH=%s:$LIBRARY_PATH make install; "%(LIBVA_INSTALL_PATH, LIBVA_INSTALL_PATH)
        print(cmd)
        os.system(cmd)

    if build_all == True or build_msdk == True:
        print("")
        print("************************************************************************")
        print_info("Build and Install Media SDK and samples", loglevelcode.INFO)
        print("************************************************************************")

        if not os.path.exists("%s/MediaSDK"%(WORKING_DIR)):
            print("MediaSDK source code doen't exist!")
            sys.exit()

        # Build and install Media SDK library and samples
        cmd ="export LIBRARY_PATH=%s:$LIBRARY_PATH; "%(LIBVA_INSTALL_PATH)
        cmd+="export C_INCLUDE_PATH=%s/include:$C_INCLUDE_PATH; "%(LIBVA_INSTALL_PREFIX)
        cmd+="export CPLUS_INCLUDE_PATH=%s/include:$CPLUS_INCLUDE_PATH; "%(LIBVA_INSTALL_PREFIX)
        cmd+="export PKG_CONFIG_PATH=%s/pkgconfig:$PKG_CONFIG_PATH;"%(LIBVA_INSTALL_PATH)
        cmd+="cd %s/MediaSDK; "%(WORKING_DIR)
        cmd+="rm -rf build; "
        cmd+="mkdir -p build; "
        cmd+="cd build; "
        cmd+="cmake ../ -DCMAKE_INSTALL_PREFIX=%s; "%(LIBVA_INSTALL_PREFIX)
        cmd+="make -j4; "
        cmd+="sudo make install; "
        print(cmd)
        os.system(cmd)

    if pre_install == True:

        if not os.path.exists("%s/MediaSDK"%(WORKING_DIR)):
            print("MediaSDK source code doen't exist!")
            sys.exit()

        #cmd = "sudo echo '/usr/lib/x86_64-linux-gnu' > /etc/ld.so.conf.d/libdrm-intel.conf; "
        cmd = "sudo echo '%s' > /etc/ld.so.conf.d/libdrm-intel.conf; "%(LIBVA_INSTALL_PATH)
        #cmd+= "sudo echo '/usr/lib' >> /etc/ld.so.conf.d/libdrm-intel.conf; "
        cmd+= "sudo ldconfig"
        print(cmd)
        os.system(cmd)

        print("************************************************************************")
        print("    Done, all installation, please reboot system !!! ")
        print("************************************************************************")

