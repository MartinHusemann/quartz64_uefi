# Script for updating the firmware on eMMC while running NetBSD

This directory contains a script that can be used to update the eMMC firmware without opening the device and booting into Maskrom mode.

The script is NetBSD specific due to the tools it uses (gpt(8) and dkctl(8)), and the way it uses them to find the dk(4) devices matching the slices on the eMMC.

The overall method could be ported to Linux or other systems easily.

The basic idea is to extract the relevant parts of the EFI image (idblock.bin, ${board}_EFI.itb), clear the old contents of the three target partitions "loader", "uboot" and "env", and then write the extracted binary new content to the partitions on the eMMC.

## Running the script

Copy the ..._EFI.img matching your machine and the roc-update-firmware.sh script. Identify the eMMC device by checking the output of dmesg(8). It should be device ld0. If you used an SD card to boot the machine, that card will show up as ld1.

Run the script like:

    ./roc-update-firmware.sh ./ROC-RK3568-PC_EFI.img ld0

## First time eMMC firmware installation

If the eMMC contents have not been updated to edk2 before, the script will complain and tell you to erase the old contents of the eMMC manually. This is a safety feature. Double check you are using the correct device!

If you are sure the device is correct, use:


to erase the old GPT on the eMMC and verify the result with


which now should say:


Erasing the GPT is only needed for the first time you install edk2 on the eMMC. Later updates will just work and re-use the existing GPT partitions.

## Additional partitions

The firmware will only use minor parts of the eMMC. You may add additional partitions with GPT index 4 and higher after the last partition created by the script.

Here is an example with such a partition added:

```
# gpt show -a ld0
      start       size  index  contents
          0          1         PMBR
          1          1         Pri GPT header
                               GUID: ecff68c3-9f1a-45ff-b657-b52cfe8b8d75
          2         32         Pri GPT table
         34         30         Unused
         64      16320      1  GPT part - Linux data
                               Type: linux-data
                               TypeID: 0fc63daf-8483-4772-8e79-3d69d8477de4
                               GUID: d9aa49b4-3502-44fe-ac30-3c94b9011f65
                               Size: 8160 K (8355840)
                               Label: loader
                               Attributes: None
      16384      16384      2  GPT part - Linux data
                               Type: linux-data
                               TypeID: 0fc63daf-8483-4772-8e79-3d69d8477de4
                               GUID: b345dd4c-d8ec-44c3-bbd7-70cecd27819d
                               Size: 8192 K (8388608)
                               Label: uboot
                               Attributes: None
      32768      32768      3  GPT part - Linux data
                               Type: linux-data
                               TypeID: 0fc63daf-8483-4772-8e79-3d69d8477de4
                               GUID: e1fc3a42-256b-4ff7-b9c3-c742cf612836
                               Size: 16384 K (16777216)
                               Label: env
                               Attributes: None
      65536  122093535      4  GPT part - NetBSD FFSv1/FFSv2
                               Type: ffs
                               TypeID: 49f48d5a-b10e-11dc-b99b-0019d1879648
                               GUID: a1427f4b-37be-492c-ab27-693ea0da0011
                               Size: 59616 M (62511889920)
                               Label: eMMC
                               Attributes: None
  122159071         32         Sec GPT table
  122159103          1         Sec GPT header
                               GUID: ecff68c3-9f1a-45ff-b657-b52cfe8b8d75
```
