#!/bin/bash

sftp root@192.168.1.7 <<EOF
cd /tmp
put examples/triangle-opengles
cd /usr/lib64
put src/libglfw.so.3.5
EOF