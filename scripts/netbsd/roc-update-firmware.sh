#! /bin/sh
#
# This script updates the firmware stored on e.g. eMMC while
# running NetBSD.
# This avoids having to open the device and switch into Maskrom
# mode.
#
# You can use a SD card to load the new firmware and boot
# the machine. Then copy this script and the EFI image
# to the machine.
#
# If this is the first time you install the edk2 firmware, destroy
# the old firmware on eMMC first by using something like
#	gpt destroy ld0
# and verify it is gone by
#	gpt show ld0
#
# If you already are using (an older version) of this edk2 firmware
# from eMMC you can use that to boot the machine, then transfer
# the new EFI image and this script to the machine. No need to
# destroy the old firmware content on eMMC, this script will deal
# and update.
#
# Run the script like:
#	./roc-update-firmware.sh IMAGE DEVICE
# for example:
#	./roc-update-firmware.sh ./ROC-RK3568-PC_EFI.img ld0
#
# The script will check the image and the device and if everything
# looks ok will erase the old data and then write the new content
# to it.
#
# Note:
#	You may add additional partitions to the eMMC device, the
#	firmware and settings store will only use tiny parts of it.
#	Additional partitions (with higher GPT index) are ignored
#	by the boot process.
#
# When the script is done, reboot and (if needed) configure the new
# firmware (all old settings have been erased).


usage()
{
	printf "usage: $0 IMAGE DEVICE\n"
	exit 1
}

if [ $# -ne 2 ]; then
	usage
fi

IMG="$1"
DEV="$2"

[ -z "${IMG}" ] && usage
[ -z "${DEV}" ] && usage


printf "Installing firmware from image $IMG to device $DEV\n"

read_details()
{
	local pfx="$1"

	awk -v pfx=$pfx '
	/Index:/	{ ndx=$2; }
	/Start:/	{ if (ndx > 0) start=$2; }
	/^Purpose: Pri GPT table$/ { printf("%s_GPT_FOUND=yes\n", pfx); }
	/Size:/		{ if (ndx > 0) size=$2; }
	/Label:/	{ if (ndx > 0 && size > 0)
				{
					if ($2 == "loader")
						name="LOADER";
					else if ($2 == "uboot")
						name="UBOOT";
					else if ($2 == "env")
						name="ENV";
					else
						name="";
					if (name != "") {
						printf("%s_%s_START=%d\n",
						    pfx, name, start);
						printf("%s_%s_SIZE=%d\n",
						    pfx, name, size);
					}
				}
			}
	'
}

match_wedges()
{
	sed -e 1d -e 's/:.* blocks at / /' -e 's/,.*//' |
	while read dev off
	do
		if [ $off -eq $DEST_LOADER_START ]; then
			printf 'DEST_LOADER_DEV="%s"\n' "$dev"
		elif [ $off -eq $DEST_UBOOT_START ]; then
			printf 'DEST_UBOOT_DEV="%s"\n' "$dev"
		elif [ $off -eq $DEST_ENV_START ]; then
			printf 'DEST_ENV_DEV="%s"\n' "$dev"
		fi
	done
}

printf " 1) finding target device partition start and size\n"
eval $( gpt show -a -p "$DEV" | read_details "DEST" )

if [ "$DEST_GPT_FOUND" != "yes" ]; then
	printf "%s does not have a GPT, creating a new one\n" "$DEV"
	gpt create "$DEV" && \
	gpt add -b 64 -i 1 -s 16320 -t linux-data -l "loader" "$DEV"
	gpt add -i 2 -s 16384 -t linux-data -l "uboot" "$DEV"
	gpt add -i 3 -s 32768 -t linux-data -l "env" "$DEV"

	eval $( gpt show -a -p "$DEV" | read_details "DEST" )
fi

[ -z "${DEST_LOADER_SIZE}" ] && [ -z "${DEST_UBOOT_SIZE}" ] && [ -z "${DEST_ENV_SIZE}" ] && {
	cat << EOM  
$DEV has a GPT but not the proper partitions -
use 'gpt destroy $DEV' and re-run this script!
EOM
	exit 1
}

printf " 2) finding source image partition start and size\n"
eval $( gpt show -a -p "$IMG" | read_details "SRC" )

printf " 3) finding target wedges\n";
eval $( dkctl "$DEV" listwedges | match_wedges )

[ -z "${SRC_LOADER_START}" ] && usage
[ -z "${SRC_LOADER_SIZE}" ] && usage
[ -z "${SRC_UBOOT_START}" ] && usage
[ -z "${SRC_UBOOT_SIZE}" ] && usage

printf " 4) extracting new contents\n";
TMP=/tmp/img.part.$$
dd if=${IMG} skip=${SRC_LOADER_START} count=${SRC_LOADER_SIZE} of=${TMP}.LOADER
dd if=${IMG} skip=${SRC_UBOOT_START} count=${SRC_UBOOT_SIZE} of=${TMP}.UBOOT

printf " 5) erasing old\n";
dd if=/dev/zero of=/dev/r${DEST_LOADER_DEV}
dd if=/dev/zero of=/dev/r${DEST_UBOOT_DEV}
dd if=/dev/zero of=/dev/r${DEST_ENV_DEV}

printf " 6) writing update\n";
dd if=${TMP}.LOADER of=/dev/r${DEST_LOADER_DEV}
dd if=${TMP}.UBOOT of=/dev/r${DEST_UBOOT_DEV}

rm -f ${TMP}.LOADER ${TMP}.UBOOT
