// FIXME: license file, or use the -l option to generate the files with the header.

package vendor.transsion.performance.sched;
import vendor.transsion.performance.sched.SchedInfo;
@VintfStability
interface ITransSched {
    // Adding return type to method instead of out param int ret since there is only one return value.
    int cancelTransSchedUxTags(in int pid);

    void dumpConfig();

    // Adding return type to method instead of out param int ret since there is only one return value.
    int dumpTransSchedUxInfo();

    // Adding return type to method instead of out param int ret since there is only one return value.
    int getCloudState();

    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    void getTransSchedScene(out SchedInfo info);

    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    void getTransSchedState(out SchedInfo info);

    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    // FIXME: AIDL does not allow String to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    void getTransSchedUxInfo(in int pid, out SchedInfo info);

    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    // FIXME: AIDL does not allow int to be an out parameter.
    // Move it to return, or add it to a Parcelable.
    void getTransSchedUxTags(in int pid, out SchedInfo info);

    // Adding return type to method instead of out param int ret since there is only one return value.
    int setTransSchedScene(in int scene);

    // Adding return type to method instead of out param int ret since there is only one return value.
    int setTransSchedState(in int state);

    // Adding return type to method instead of out param int ret since there is only one return value.
    int setTransSchedUxPrio(in int pid, in int shift);

    // Adding return type to method instead of out param int ret since there is only one return value.
    int setTransSchedUxTags(in int pid, in int uxTags);

    // Adding return type to method instead of out param int ret since there is only one return value.
    int setTransSchedUxTagsByName(in int pid, in String mainThread);

    // Adding return type to method instead of out param int ret since there is only one return value.
    int cancelTransSchedUxTagsByName(in int pid, in String mainThread);
}
