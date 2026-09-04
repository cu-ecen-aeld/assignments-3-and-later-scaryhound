#!/bin/sh
case "$1" in 
	start)
		echo "Loading aesdchar and Starting aesdsocket"
		/usr/bin/aesdchar_load
		start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
		;;
	stop)
		echo "Stopping aesdsocket and unloading aesdchar"
		start-stop-daemon -K -s SIGTERM -n aesdsocket
		/usr/bin/aesdchar_unload
		;;
	*)
		echo "Usage: $0 {start|stop}"
		exit 1	
esac
exit 0
