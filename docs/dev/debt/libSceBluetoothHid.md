# libSceBluetoothHid technical debt

### Silent stubs

- [libSceBluetoothHid](../../../core/libs/prx/libSceBluetoothHid/Export.cpp) implements only the no-device path of firmware 13.20's libSceBluetoothHid: no device ever connects, so the callback is never called. The argument checks, their order and the 0x8154xxxx errors follow its disassembly; the `/dev/bt` results for no device were measured on a retail PS5 with none paired: an unknown device's name answers 0x81540005, input and output reports 0x8154000d, and device 0's name succeeds after clearing 121 bytes. A thread param is checked with `scePthreadAttrSetaffinity` and `scePthreadAttrSetschedparam`; no watcher thread is created

### Unknown function info

- [sceBluetoothHidRegisterDevice and sceBluetoothHidUnregisterDevice](../../../core/libs/prx/libSceBluetoothHid/Export.cpp) (libSceBluetoothHid) - they succeed. Measured from a payload, `/dev/bt` answered 0x81540002 to every vendor and product id, including 0:0 and ids never registered, which reads as the driver refusing a process that is not an application; the result for a title is unverified. The meaning of bit 48 of a device id (the report id is the first data byte) and of the 121 bytes cleared for device 0 is unknown, and the console rejects an affinity mask of 0 (0x8002000b), which libkernel's `scePthreadAttrSetaffinity` accepts
