
qrencode: qrencode.c 
	gcc -Wall qrencode.c -o qrencode -lqrencode -lgdk_pixbuf-2.0 $(pkg-config --cflags --libs gstreamer-1.0) -I/usr/include/gdk-pixbuf-2.0

canon: gst_appsrc_canon.c
	gcc -Wall gst_appsrc_canon.c -o canon $(pkg-config --cflags --libs gstreamer-1.0) -I/usr/include/gstreamer-1.0/ -I/usr/include/glib-2.0/ -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/

appsink: appsink.c
	gcc -Wall appsink.c -o appsink -lqrencode -lgdk_pixbuf-2.0 -lgstapp-1.0 $(pkg-config --cflags --libs gstreamer-1.0) -I/usr/include/gdk-pixbuf-2.0

clean:
	rm -rf qrencode canon *.png
