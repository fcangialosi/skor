### Dependencies ###

* gstreamer 1.x
* v4l2loopback
* libzbar
* libqrencode
* automake and libtool to build the gstreamer plugins
* Skype

### Set up ###

* Build the gstreamer plugins (skorsrc and skorsink)
* Add /usr/local/lib/gstreamer-1.0/ to the GST_PLUGIN_PATH env variable
* Install the V4L2loopback device
* Build and run "tunnel" in the tunnel subdirectory
