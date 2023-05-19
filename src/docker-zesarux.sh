#!/bin/bash

#    ZEsarUX  ZX Second-Emulator And Released for UniX
#    Copyright (C) 2013 Cesar Hernandez Bano
#
#    This file is part of ZEsarUX.
#
# Just a simple utility to test ZEsarUX in docker

docker-build() {
	docker build --tag=zesarux --progress plain .
}


docker-build-version() {
	VERSIONNAME=$1
	docker build --tag=zesarux.$VERSIONNAME --progress plain -f Dockerfile.$VERSIONNAME .
}


docker-build-and-get-binary() {
	VERSIONNAME=$1

                echo "-----Building image"
                docker rm run-zesarux-$VERSIONNAME

                docker-build-version $VERSIONNAME

                echo
                echo "-----Running tests"
                sleep 1
                docker run -it --entrypoint /zesarux/src/automatic_tests.sh zesarux.$VERSIONNAME 
                #docker run -it  zesarux.$VERSIONNAME --codetests
		if [ $? != 0 ]; then
			echo "codetests failed"
			exit 1
		fi

                echo
                echo "-----Running image"
                docker run --name run-zesarux-$VERSIONNAME -it zesarux.$VERSIONNAME --vo stdout --ao null --exit-after 1
                echo
                echo "-----Getting executables and install file"
                docker cp run-zesarux-$VERSIONNAME:/zesarux/src/zesarux zesarux.$VERSIONNAME
                docker cp run-zesarux-$VERSIONNAME:/zesarux/src/install.sh install.sh.$VERSIONNAME
                echo
                echo "-----$VERSIONNAME Binary file is: "
                ls -lha zesarux.$VERSIONNAME
}

help() {
	echo "$0 [build|build-version|build-version-and-get-binary|clean-cache|codetests|run|run-curses|run-mac-xorg|run-xorg]"
	echo "build-version and build-version-and-get-binary require a parameter, one of: [debian|ubuntu]"
}

if [ $# == 0 ]; then
	help
	exit 1
fi

case $1 in
	
	clean-cache)
		docker builder prune
	;;

	build)
		docker-build
	;;

	build-version)
		if [ $# == 1 ]; then
			echo "A parameter version is required"
			exit 1
		fi

		docker-build-version $2
	;;

	build-version-and-get-binary)
		if [ $# == 1 ]; then
			echo "A parameter version is required"
			exit 1
		fi

		docker-build-and-get-binary $2
	;;

	run)
		docker-build
		docker run -it zesarux
	;;

	run-curses)
		docker-build
		docker run -it zesarux --ao null --vo curses
	;;

	run-xorg)
		docker-build
		docker run -it -e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix --user="$(id --user):$(id --group)" zesarux --disableshm --ao null
	;;

	run-mac-xorg)
		docker-build
		export HOSTNAME=`hostname`
		xhost +
		echo "Be sure that xquarz preference Allow connections from network clients is enabled"
		docker run -it -e DISPLAY=${HOSTNAME}:0 -v /tmp/.X11-unix:/tmp/.X11-unix  zesarux --disableshm --ao null
	;;


	codetests)
		docker-build
		docker run -it zesarux --codetests
	;;

	*)
		help
	;;

esac

