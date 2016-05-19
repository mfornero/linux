#!/usr/bin/env python2
# syntax: <gen_dtb.py> dts
import os, sys, imp, subprocess

def generate_dtb(dts):
    # Tool paths
    DTC = "dtc"
    # This script lives in ./scripts/dtc
    SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
    LINUX_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
    LINUX_ARM_DTS = LINUX_DIR + "/arch/arm/boot/dts"
    LINUX_INCLUDE = LINUX_DIR + "/include"

    INCLUDE_DIRS = ["%s" % (LINUX_ARM_DTS),
                "%s/dts" % (LINUX_INCLUDE),
                "%s/include" % (LINUX_ARM_DTS)]

    DEPENDENCY_FILE = "dependency.pre.tmp"
    DTC_CPP_PRE_FLAGS = "-Wp,-MD,%s -nostdinc" % (DEPENDENCY_FILE)
    DTC_CPP_POST_FLAGS = "-undef -D__DTS__ -x assembler-with-cpp"


    app = dts.replace(".dts", "")
    dtb_file = dts.replace(".dts", ".dtb")

    include_dirs = list(INCLUDE_DIRS)
    
    # build the cpp command
    dtc_cpp_flags = list()
    dtc_cpp_flags.extend(DTC_CPP_PRE_FLAGS.split())
    for inc in include_dirs:
        inc_str = "-I" + inc
        dtc_cpp_flags.append(inc_str)
    dtc_cpp_flags.extend(DTC_CPP_POST_FLAGS.split())
        
    # Run the DTS through CPP     
    tmpFile = app + ".tmp.dts"    
    args = ["cpp"]
    args.extend(dtc_cpp_flags)
    args.extend(["-o", tmpFile, dts])
    subprocess.call(args)

    # Call DTC
    args = [DTC]
    for inc in include_dirs:
        args.extend(["-i", inc])
    strArgs = "-@ -I dts -O dtb -o %s %s" % (dtb_file, tmpFile)
    args.extend(strArgs.split())
    subprocess.call(args)

    # Cleanup
    os.remove(DEPENDENCY_FILE)
    os.remove(tmpFile)

generate_dtb(sys.argv[1])
########################################
# Module Globals
########################################

