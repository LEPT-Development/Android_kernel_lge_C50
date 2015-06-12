#guilbert.lee@lge.com Mon 28 Jan 2013
#lg dts viewer

#!/bin/sh

DTS_PATH=$1
WORK_PATH=${0%/*}


if [ "$DTS_PATH" = "" ]
then
    echo "usage : ./lg_dts_viewer.sh [dts path]"

    echo "dts path : path of dts file"
    echo "ex(in Kernel root) :  ./scripts/lg_dt_viewer/\
lg_dts_viewer.sh arch/arm64/boot/dts/lge/msm8939-altev2_vzw/msm8939-altev2_vzw.dts"
    exit
fi

DTS_NAME=${DTS_PATH##*/}

if [ "$DTS_NAME" = "" ]
then
   echo "usage : Invaild DTS path, Cannot find *.dts file"
   exit
fi

OUT_PATH_ORG=${DTS_NAME/%.dts/}
OUT_PATH=out_$OUT_PATH_ORG
ARM32_64_PATH=${DTS_PATH#*/}
ARM32_64_PATH=${ARM32_64_PATH%%/*}

if [ ! -d "$OUT_PATH" ] ; then
	mkdir $OUT_PATH
fi

# For support #include, C-pre processing first to dts.
gcc -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
		-I arch/$ARM32_64_PATH/boot/dts/ -I arch/$ARM32_64_PATH/boot/dts/include/ \
		$DTS_PATH -o $OUT_PATH/$DTS_NAME.preprocessing

# Now, transfer to dts from dts with LG specific

${WORK_PATH}/lg_dtc -o $OUT_PATH/$DTS_NAME\
 -D -I dts -O dts -H specific -s ./$OUT_PATH/$DTS_NAME.preprocessing
${WORK_PATH}/lg_dtc -o $OUT_PATH/$DTS_NAME.2\
 -D -I dts -O dts -H specific2 -s ./$OUT_PATH/$DTS_NAME.preprocessing
