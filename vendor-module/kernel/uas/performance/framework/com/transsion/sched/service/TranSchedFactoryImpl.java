/**
 * Copyright (C) 2023 transsion  Inc
 * add for tran_sched
 * @author xianhe.zhou@transsion.com
 * @version 1.0,  03/2023
 **/

package com.transsion.sched.service;

import com.transsion.server.sched.TranSchedFactory;

public class TranSchedFactoryImpl extends TranSchedFactory {

    private ITranSchedService schedService;

    public void systemReady() {
        schedService = new TranSchedService();
    }

    public boolean isReady() {
        return schedService.isReady();
    }

    public boolean setTranSchedState(int state) {
        return schedService.setTranSchedState(state);
    }

    public int getTranSchedState() {
        return schedService.getTranSchedState();
    }

    public boolean setTranSchedScene(int scene) {
        return schedService.setTranSchedScene(scene);
    }

    public int getTranSchedScene() {
        return schedService.getTranSchedScene();
    }

    public boolean setTranSchedGroup(int pid, boolean is_uxgroup) {
        return schedService.setTranSchedGroup(pid, is_uxgroup);
    }

    public boolean setTranSchedUxTagsByName(int pid, String mainThread) {
        return schedService.setTranSchedUxTagsByName(pid, mainThread);
    }

    public boolean setTranSchedUxTags(int pid, long uxTags) {
        return schedService.setTranSchedUxTags(pid, uxTags);
    }

    public boolean cancelTranSchedUxTags(int pid) {
        return schedService.cancelTranSchedUxTags(pid);
    }

    public long getTranSchedUxTags(int pid) {
        return schedService.getTranSchedUxTags(pid);
    }

    public String getTranSchedUxInfo(int pid) {
        return schedService.getTranSchedUxInfo(pid);
    }

    public boolean dumpTranSchedUxInfo() {
        return schedService.dumpTranSchedUxInfo();
    }

}
