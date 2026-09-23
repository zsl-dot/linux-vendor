INTERFACES_ROOT=vendor/transsion/spd/uas/performance
PACKAGE_ROOT=vendor.transsion.performance
MODULE=sched
PACKAGE_FULL=${PACKAGE_ROOT}.${MODULE}@1.0
LOC=$INTERFACES_ROOT/$MODULE/1.0/tmp/
hidl-gen -L androidbp -r $PACKAGE_ROOT:$INTERFACES_ROOT -r android.hidl:system/libhidl/transport $PACKAGE_FULL
hidl-gen -o $LOC  -L androidbp-impl -r $PACKAGE_ROOT:$INTERFACES_ROOT -r android.hidl:system/libhidl/transport $PACKAGE_FULL
hidl-gen -o $LOC  -L c++-impl -r $PACKAGE_ROOT:$INTERFACES_ROOT -r android.hidl:system/libhidl/transport $PACKAGE_FULL
hidl-gen -o $LOC  -L c++-impl-headers -r $PACKAGE_ROOT:$INTERFACES_ROOT -r android.hidl:system/libhidl/transport $PACKAGE_FULL
hidl-gen -o $LOC  -L vts -r $PACKAGE_ROOT:$INTERFACES_ROOT -r android.hidl:system/libhidl/transport $PACKAGE_FULL
