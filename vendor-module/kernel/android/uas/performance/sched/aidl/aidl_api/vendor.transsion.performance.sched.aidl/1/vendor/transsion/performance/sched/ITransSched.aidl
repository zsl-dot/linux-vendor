// FIXME: license file, or use the -l option to generate the files with the header.
///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually. There are
// two cases:
// 1). this is a frozen version file - do not edit this in any case.
// 2). this is a 'current' file. If you make a backwards compatible change to
//     the interface (from the latest frozen version), the build system will
//     prompt you to update this file with `m <name>-update-api`.
//
// You must not make a backward incompatible change to any AIDL file built
// with the aidl_interface module type with versions property set. The module
// type is used to build AIDL files in a way that they can be used across
// independently updatable components of the system. If a device is shipped
// with such a backward incompatible change, it has a high risk of breaking
// later when a module using the interface is updated, e.g., Mainline modules.

package vendor.transsion.performance.sched;
@VintfStability
interface ITransSched {
  int cancelTransSchedUxTags(in int pid);
  void dumpConfig();
  int dumpTransSchedUxInfo();
  int getCloudState();
  void getTransSchedScene(out vendor.transsion.performance.sched.SchedInfo info);
  void getTransSchedState(out vendor.transsion.performance.sched.SchedInfo info);
  void getTransSchedUxInfo(in int pid, out vendor.transsion.performance.sched.SchedInfo info);
  void getTransSchedUxTags(in int pid, out vendor.transsion.performance.sched.SchedInfo info);
  int setTransSchedScene(in int scene);
  int setTransSchedState(in int state);
  int setTransSchedUxPrio(in int pid, in int shift);
  int setTransSchedUxTags(in int pid, in int uxTags);
  int setTransSchedUxTagsByName(in int pid, in String mainThread);
}
