#!/bin/bash

# -------------------------------------------------------------------------------
#
#   Copyright (C) 2017 Cisco Talos Security Intelligence and Research Group
#
#   PyREBox: Python scriptable Reverse Engineering Sandbox
#   Author: Xabier Ugarte-Pedrero
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License version 2 as
#   published by the Free Software Foundation
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
#   MA 02110-1301, USA.
#
# -------------------------------------------------------------------------------

root_path=$(pwd)
pyrebox_path=$root_path/pyrebox
volatility_path=$root_path/volatility
qemu_path=$root_path/qemu
show_help="no"
debug="no"
rebuild_vol="no"
reconfigure="no"
jobs=8

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

for opt do
  optarg=`expr "x$opt" : 'x[^=]*=\(.*\)'`
  case "$opt" in
  --help|-h) show_help="yes"
  ;;
  --debug) debug="yes"
  ;;
  --reconfigure) reconfigure="yes"
  ;;
  --rebuild_volatility) rebuild_vol="yes"
  ;;
  --jobs=*) jobs=$optarg
  ;;
  *) echo "ERROR: unknown option $opt"; show_help="yes"
  ;;
  esac
done

if test x"$show_help" = x"yes" ; then
cat << EOF
Usage: configure [options]
Options: [defaults in brackets after descriptions]
EOF
echo "Standard options:"
echo "  --help                       print this message"
echo "  --debug                      compile for debug"
echo "  --jobs=n                     build using n parallel processes"
echo "  --reconfigure                reconfigure pyrebox"
echo "  --rebuild_volatility         delete current volatility/ and rebuild it"
echo ""
exit 1
fi

#----------------------- QEMU -----------------------
if [ x"${reconfigure}" = xyes ] || [ ! -f ${qemu_path}/config-host.mak ]; then
    echo -e "\n${GREEN}[*] Configuring qemu...${NC}\n"
    cd ${qemu_path}
    qemu_configure_flags=""
    if [ x"${debug}" = xyes ]
    then
      qemu_configure_flags='--enable-debug'
    fi
    ./configure --disable-docs --disable-libiscsi --target-list=i386-softmmu,x86_64-softmmu ${qemu_configure_flags}
    if [ $? -ne 0 ]; then
        echo -e "\n${RED}[!] Could not configure QEMU${NC}\n"
        exit 1
    fi

    #----------------------- PYREBOX -----------------------

    echo -e "\n${GREEN}[*] Configuring pyrebox...${NC}\n"

    config_h=$pyrebox_path/config.h
    test -f $config_h && rm $config_h
    echo "//# Automatically generated by configure - do not modify" > $config_h
    echo "#define VOLATILITY_PATH \"$volatility_path\"" >> $config_h
    echo "#define PYREBOX_PATH \"$pyrebox_path\"" >> $config_h
    echo "#define ROOT_PATH \"$root_path\"" >> $config_h

fi

if [ x"${rebuild_vol}" = xyes ]; then
    rm -rf $volatility_path
fi

#----------------------- VOLATILITY -----------------------

cd ${root_path}

if ! [ -d "${volatility_path}" ] 
then
    echo -e "\n${GREEN}[*] Cloning volatility...${NC}\n"
    git clone https://github.com/volatilityfoundation/volatility volatility
    if ! [ -d "${volatility_path}" ]; then
        echo -e "\n${RED}[!] Volatility could not be cloned from github!${NC}\n"
        exit 1
    fi
    cd $volatility_path
    git checkout 2.6
    if [ $? -ne 0 ]; then
        echo -e "\n${RED}[!] Could not checkout the appropriate version of volatility${NC}\n"
        exit 1 
    fi
    echo -e "\n${GREEN}[*] Patching volatility...${NC}\n"
    cd $volatility_path
    git apply $pyrebox_path/third_party/volatility/conf.py.patch
    if [ $? -ne 0 ]; then
        echo -e "\n${RED}[!] Could not patch volatility${NC}\n"
        exit 1
    fi
    cp $pyrebox_path/third_party/panda/pmemaddressspace.py $volatility_path/volatility/plugins/addrspaces
    if [ $? -ne 0 ]; then
        echo -e "\n${RED}[!] Could not patch volatility${NC}\n"
        exit 1 
    fi
fi

#----------------------- PYREBOX -----------------------

echo -e "${GREEN}\n[*] Building pyrebox...${NC}\n"
cd $root_path
make -j${jobs}
if ! [ -f qemu/i386-softmmu/qemu-system-i386 ]; then
    echo -e "${RED}\n[!] Oops... build failed!${NC}\n"
    exit 1
fi
if ! [ -f qemu/x86_64-softmmu/qemu-system-x86_64 ]; then
    echo -e "${RED}\n[!] Oops... build failed!${NC}\n"
    exit 1
fi
echo -e "${GREEN}\n[*] Creating symbolic links...${NC}\n"
ln -sf qemu/i386-softmmu/qemu-system-i386 pyrebox-i386
ln -sf qemu/x86_64-softmmu/qemu-system-x86_64 pyrebox-x86_64
echo -e "${GREEN}\n\n[*] Done, enjoy!${NC}"
