
REPROBUILD=no
SECUREBUILD=no
POLLUXCP=no
DATE=20190826
POLLUXDIR=/tftpboot/aristainetos/$DATE
SECUREPATH=/home/hs/data/Entwicklung/abb/uboot-rework/secure/cst-2.3.3/linux64/bin
POSITIONAL=()

while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -c|--copypollux)
    POLLUXCP=yes
    shift # past argument
    ;;
    --polluxdir)
    POLLUXDIR="$2"
    shift # past argument
    shift # past value
    ;;
    -r|--reprobuild)
    REPROBUILD=yes
    shift # past argument
    ;;
    -s|--securebuild)
    SECUREBUILD=yes
    shift # past argument
    ;;
    --default)
    DEFAULT=YES
    shift # past argument
    ;;
    *)    # unknown option
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done

echo Copy Pollux $POLLUXCP
echo Polluxdir $POLLUXDIR
echo Reprobuild $REPROBUILD
echo secure path $SECUREPATH

CONFIGS="aristainetos2_defconfig aristainetos2b_defconfig aristainetos2bcsl_defconfig aristainetos2c_defconfig"
#CONFIGS="aristainetos2b_defconfig"
CONFIGS="aristainetos2_defconfig"

CHECK=$(printenv CROSS_COMPILE)
if [ -z $CHECK ]; then
        echo no cross compiler please set
	echo export PATH=/home/hs/toolchain/linaro/gcc-linaro-7.2.1-2017.11-i686_arm-linux-gnueabi/bin:\$PATH
        echo export ARCH=arm
        echo export CROSS_COMPILE=arm-linux-gnueabi-
	echo
	echo on xmg
        echo export PATH=/work/tbot2go/toolchains/armv7-eabihf--glibc--bleeding-edge/bin:\$PATH
        echo export ARCH=arm
        echo export CROSS_COMPILE=arm-linux-
        exit 0
fi

for DNAME in $CONFIGS
do
	BDIR=/work/hs/compile/u-boot/$DNAME
	echo BDIR: $BDIR

	if [ $REPROBUILD == "yes" ]; then
		rm -rf $BDIR
	fi

	SUBDIR=$DATE-$DNAME

	TDIR=/tftpboot/aristainetos2/$SUBDIR
	TDIR=/home/hs/data/Entwicklung/abb/uboot-rework/results/$SUBDIR
	echo TDIR: $TDIR

	if [ -f $BDIR/.config ]; then
        	echo $BDIR configured
	else
	        echo $BDIR not configured
        	make O=$BDIR $DNAME
	fi

	if [ $REPROBUILD == "yes" ]; then
		SOURCE_DATE_EPOCH=0 KCFLAGS=-Werror make O=$BDIR -j8 all
	else
		# treat warnings as errors
		KCFLAGS=-Werror make O=$BDIR -j8 V=1 all
		#KCFLAGS=-Werror make O=$BDIR -j8 all
	fi
	if [ $? -ne 0 ]; then
	        echo compile failed
	        exit 1
	fi

	make O=$BDIR savedefconfig
	cp $BDIR/defconfig configs/$DNAME

	if [ ! -d $TDIR ]; then
	        echo dir $TDIR does not exist
	        mkdir $TDIR
	fi

	echo copy files from $BDIR to $TDIR

	hexdump -C $BDIR/u-boot.bin> $TDIR/hexdump-u-boot.bin

	if [ $SECUREBUILD == "yes" ]; then
		echo creating secure boot image
		cp $BDIR/u-boot-dtb.imx $SECUREPATH
		cd $SECUREPATH
		./cst --o u-boot-dtb_csf.bin --i u-boot-dtb.csf
		if [ $? -ne 0 ]; then
			echo error calling cst tool in $SECUREPATH
		fi
		cat u-boot-dtb.imx u-boot-dtb_csf.bin > u-boot-dtb.imx.signed
		cp u-boot-dtb.imx.signed $TDIR
		echo create testfile for hab
		dd if=/dev/urandom bs=1024 count=10 > testhabfile.random
		$BDIR/tools/mkimage -T script -C none -n 'Demo Script File' -d testhabfile.random testhabfile.bin
		# pad to next 4k
		objcopy -I binary -O binary --pad-to=0x3000 --gap-fill=0x00 testhabfile.bin testhabfile-pad.bin
		./genIVT-testfile
		cat testhabfile-pad.bin ivt.bin > testhabfile-pad-ivt.bin
		./cst --o testhabfile_csf.bin --i testhabfile.csf
		cat testhabfile-pad-ivt.bin testhabfile_csf.bin > testhabfile-pad-ivt.bin.signed
		# create file with error
		cp testhabfile-pad-ivt.bin.signed testhabfile-pad-ivt.bin.signed.error
		./set-byte testhabfile-pad-ivt.bin.signed.error 0x10 0
		./set-byte testhabfile-pad-ivt.bin.signed.error 0x11 2
		./set-byte testhabfile-pad-ivt.bin.signed.error 0x12 1
		# copy files
		cp testhabfile-pad-ivt.bin.signed $TDIR
		cp testhabfile-pad-ivt.bin.signed.error $TDIR
		cp set-byte $TDIR
		cp u-boot-dtb.csf $TDIR
		cp testhabfile.csf $TDIR
		cp genIVT-testfile $TDIR
		# create bootscript
		$BDIR/tools/mkimage -T script -C none -n 'Demo Script File' -d $BDIR/source/board/aristainetos/boot.scr boot.scr.bin
		objcopy -I binary -O binary --pad-to=0x1000 --gap-fill=0x00 boot.scr.bin boot-pad.scr.bin
		./genIVT-bootscript
		cat boot-pad.scr.bin ivt.bin > boot-pad-ivt.scr.bin
		./cst --o boot_csf.bin --i bootscript.csf
		cat boot-pad-ivt.scr.bin boot_csf.bin > boot-pad-ivt.scr.bin.signed

		cp boot-pad-ivt.scr.bin.signed $TDIR
		cp genIVT-bootscript $TDIR
		cp bootscript.csf $TDIR
		cd $BDIR
		echo check if $SECUREPATH/u-boot-dtb.csf has the correct setting
		echo for HAB block
	fi
	#cp $BDIR/u-boot.imx $TDIR
	cp $BDIR/u-boot-dtb.imx $TDIR
	cp $BDIR/u-boot.bin $TDIR
	cp $BDIR/System.map $TDIR
	if [ $REPROBUILD == "yes" ]; then
		cmp $TDIR/u-boot.bin $TDIR/original/u-boot.bin
		if [ $? -ne 0 ]; then
	        	echo diff in resulting binary
		        exit 1
		else
			echo binaries are the same as original
		fi
	fi

	if [ $POLLUXCP == "yes" ]; then
	if [ $POLLUXDIR != "no" ]; then
		if [ $DNAME == "aristainetos2_defconfig" ]; then
			echo copy to pollux $POLLUXDIR
			#scp $TDIR/u-boot.imx pollux.denx.org:$POLLUXDIR
			${CROSS_COMPILE}objdump -S $BDIR/u-boot > $TDIR/objdump-u-boot
			if [ $SECUREBUILD == "yes" ]; then
				scp $TDIR/u-boot-dtb.imx.signed pollux.denx.org:$POLLUXDIR
				scp $TDIR/testhabfile-pad-ivt.bin.signed pollux.denx.org:$POLLUXDIR
				scp $TDIR/testhabfile-pad-ivt.bin.signed.error pollux.denx.org:$POLLUXDIR
				scp $TDIR/boot-pad-ivt.scr.bin.signed pollux.denx.org:$POLLUXDIR
				scp $TDIR/u-boot-dtb.imx.signed pollux.denx.org:/tftpboot/aristainetos/tbot
				scp $TDIR/testhabfile-pad-ivt.bin.signed pollux.denx.org:/tftpboot/aristainetos/tbot
				scp $TDIR/testhabfile-pad-ivt.bin.signed.error pollux.denx.org:/tftpboot/aristainetos/tbot
				scp $TDIR/boot-pad-ivt.scr.bin.signed pollux.denx.org:/tftpboot/aristainetos/tbot
			fi
			scp $TDIR/u-boot-dtb.imx pollux.denx.org:$POLLUXDIR
			scp $TDIR/u-boot.bin pollux.denx.org:$POLLUXDIR
			scp $TDIR/System.map pollux.denx.org:$POLLUXDIR
		fi
	fi
	fi
done

echo Boot from SPI NOR flash:
echo relais   relsrv-02-03  1  off
echo
echo Boot from SD card:
echo relais   relsrv-02-03  1  on
echo
echo telnet bdi12
echo config aristainetos/aristainetos.cfg
echo
echo "mw 0x12000000 0 0x4000;tftp 0x12000000 aristainetos/"$DATE"/env.txt;env import -t 0x12000000"
echo run upd_uboot
echo oder for update U-Boot on sd card
echo run upd_uboot_sd
echo
echo notiz:
echo "in bdi: load 0x17800000 aristainetos/"$DATE"/u-boot.bin BIN"
echo "t 0x17800000;g"
echo
echo "debug u-boot.imx"
echo "load 0x177ff400 aristainetos/"$DATE"/u-boot.imx BIN"
echo "MMH  0x177ffffc 0xa000;MMH  0x177ffffe 0x4700"
echo "t 0x177ffffc"

if [ $SECUREBUILD == "yes" ]; then
	echo "tftp 0x10000000 aristainetos/"$DATE"/testhabfile-pad-ivt.bin.signed"
	echo "tftp 0x10000000 aristainetos/"$DATE"/testhabfile-pad-ivt.bin.signed.error"
	echo "tftp 0x10000000 aristainetos/"$DATE"/boot-pad-ivt.scr.bin.signed"
	echo "hab_auth_img 10000000 \${filesize}"
fi

exit 0

cp $BDIR/spl/boot.bin $TDIR
cp $BDIR/spl/u-boot-spl.bin $TDIR
