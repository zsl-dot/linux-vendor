#!/bin/bash

source system/tools/hidl/update-makefiles-helper.sh

mydir=$(dirname $0 | sed -e 's/^\.\///')
echo "${mydir}"
do_makefiles_update \
  "vendor.transsion.performance:${mydir}" \
  "android.hardware:hardware/interfaces" \
  "android.hidl:system/libhidl/transport"
