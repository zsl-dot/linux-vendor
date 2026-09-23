/**
 * Copyright (C) 2023 transsion  Inc
 * add for tran_sched
 * @author xianhe.zhou@transsion.com
 * @version 1.0,  03/2023
 **/


package com.transsion.sched.service;


import android.util.Slog;
import java.util.function.Supplier;
import android.os.IHwBinder;
import vendor.transsion.performance.sched.V1_0.ITransSched;


public class TranSchedService implements IHwBinder.DeathRecipient, ITranSchedService {
    private static final String TAG = "JAR_TranSchedService";
    private ITransSched tranSchedInstance = null;
    private final Supplier<ITransSched> mLazyTranSchedInstance;


    public TranSchedService() {
        mLazyTranSchedInstance = this::transSchedProduct;
    }

    public boolean isReady() {
        return getTranSchedInstance() != null;
    }


    public boolean setTranSchedState(int state) {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.setTransSchedState(state) >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        return false;
    }


    private static class GetTransSchedStateCbImpl implements ITransSched.getTransSchedStateCallback {
        int resCode = -1;
        int resContent = 0;

        public void onValues(int status, int content) {
            resCode = status;
            resContent = content;
        }
    }


    public int getTranSchedState() {
        ITransSched transSched;
        GetTransSchedStateCbImpl callback = new GetTransSchedStateCbImpl();

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            transSched.getTransSchedState(callback);
        } catch (Exception e) {
            Slog.wtf(TAG, "getTranSchedState call TransSched device error: " + e);
        }

        if (callback.resCode < 0) {
            Slog.e(TAG, "getTranSchedState result error.");
            return callback.resCode;
        }

        return callback.resContent;
    }


    public boolean setTranSchedScene(int scene) {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.setTransSchedScene(scene) >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        return false;
    }


    private static class GetTransSchedSceneCbImpl implements ITransSched.getTransSchedSceneCallback {
        int resCode = -1;
        int resContent = 0;

        public void onValues(int status, int content) {
            resCode = status;
            resContent = content;
        }
    }


    public int getTranSchedScene() {
        ITransSched transSched;
        GetTransSchedSceneCbImpl callback = new GetTransSchedSceneCbImpl();

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            transSched.getTransSchedScene(callback);
        } catch (Exception e) {
            Slog.wtf(TAG, "getTranSchedScene call TransSched device error: " + e);
        }

        if (callback.resCode < 0) {
            Slog.e(TAG, "getTranSchedScene result error.");
            return callback.resCode;
        }

        return callback.resContent;
    }


    public boolean setTranSchedGroup(int pid, boolean is_uxgroup) {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.setTransSchedGroup(pid, is_uxgroup ? 1 : 0) >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        return false;
    }


    public boolean setTranSchedUxTagsByName(int pid, String mainThread) {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.setTransSchedUxTagsByName(pid, mainThread) >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        return false;
    }


    public boolean setTranSchedUxTags(int pid, long uxTags) {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.setTransSchedUxTags(pid, (int) uxTags) >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        return false;
    }


    public boolean cancelTranSchedUxTags(int pid) {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.cancelTransSchedUxTags(pid) >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        return false;
    }


    private static class GetTransSchedUxTagsCbImpl implements ITransSched.getTransSchedUxTagsCallback {
        int resCode = -1;
        int resContent = 0;

        public void onValues(int status, int content) {
            resCode = status;
            resContent = content;
        }
    }


    public long getTranSchedUxTags(int pid) {
        ITransSched transSched;
        GetTransSchedUxTagsCbImpl callback = new GetTransSchedUxTagsCbImpl();

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            transSched.getTransSchedUxTags(pid, callback);
        } catch (Exception e) {
            Slog.wtf(TAG, "call TransSched device error: " + e);
        }

        if (callback.resCode < 0) {
            Slog.e(TAG, "getTranSchedUxTags result error.");
            return callback.resCode;
        }

        return callback.resContent;
    }


    private static class GetTransSchedUxInfoCbImpl implements ITransSched.getTransSchedUxInfoCallback {
        int resCode = -1;
        String resContent = null;

        public void onValues(int status, String content) {
            resCode = status;
            resContent = content;
        }
    }


    public String getTranSchedUxInfo(int pid) {
        ITransSched transSched;
        GetTransSchedUxInfoCbImpl callback = new GetTransSchedUxInfoCbImpl();

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            transSched.getTransSchedUxInfo(pid, callback);
        } catch (Exception e) {
            Slog.wtf(TAG, "Call TransSched device error: " + e);
        }

        if (callback.resCode < 0) {
            Slog.e(TAG, "getTranSchedUxInfo result error.");
            return null;
        }

        return callback.resContent;
    }


    public boolean dumpTranSchedUxInfo() {
        ITransSched transSched;

        try {
            transSched = getTranSchedInstance();
            assert transSched != null;
            return transSched.dumpTransSchedUxInfo() >= 0;
        } catch (Exception e) {
            Slog.wtf(TAG, "dumpTranSchedUxInfo call TransSched device error: " + e);
        }

        return false;
    }


    private ITransSched getTranSchedInstance() {
        return mLazyTranSchedInstance.get();
    }


    private synchronized ITransSched transSchedProduct() {
        if (tranSchedInstance == null) {
            try {
                tranSchedInstance = ITransSched.getService("default");
            } catch (Exception e) {
                Slog.w(TAG, "TransSched hidl service is not ready, get TransSched interface exception: " + e);
                tranSchedInstance = null;
                return null;
            }

            if(getTranSchedInstance() != null) {
                getTranSchedInstance().asBinder().linkToDeath(this, 0);
            }
        }

        return tranSchedInstance;
    }


    public void serviceDied(long cookie) {
        tranSchedInstance = null;
        Slog.e(TAG, "TransSched hardware service died");
    }
}
