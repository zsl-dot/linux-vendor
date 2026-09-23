# Adding modules to compilation

## for kernel-5.10
1. Add the following config to the file "device/transsion/xxx/ko_order_table.csv"
    ```
    trans_sched.ko,/../vendor/transsion/spd/uas/performance/kernel_module/build/kernel-5.x/trans_sched.ko,vendor,Y,N,user/userdebug/eng
    ```
2. Add the following config to the file "kernel-5.10/kernel/configs/ext_modules.list"
    ```
    ../vendor/transsion/spd/uas/performance/kernel_module/build/kernel-5.x
    ```

## for kernel-4.19
1. Add the following config to the file "kernel-4.19/kernel/Makefile"
    ```
    obj-y += ../../vendor/transsion/spd/uas/performance/kernel_module/build/kernel-4.x/
    ```
