/**
 * Copyright (C) 2023 transsion  Inc
 * add for tran_sched
 * @author xianhe.zhou@transsion.com
 * @version 1.0,  03/2023
 **/

package com.transsion.sched.service;

public interface ITranSchedService {

    boolean isReady();

    boolean setTranSchedState(int state);

    int getTranSchedState();

    boolean setTranSchedScene(int scene);

    int getTranSchedScene();

    boolean setTranSchedGroup(int pid, boolean is_uxgroup);

    boolean setTranSchedUxTagsByName(int pid, String mainThread);

    boolean setTranSchedUxTags(int pid, long uxTags);

    boolean cancelTranSchedUxTags(int pid);

    long getTranSchedUxTags(int pid);

    String getTranSchedUxInfo(int pid);

    boolean dumpTranSchedUxInfo();
}
