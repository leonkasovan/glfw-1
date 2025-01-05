#!/bin/bash

cp -r /home/deck/Projects/glfw-plus/src /home/deck/Projects/glfw-go/v3.5/glfw/glfw/
cp -r /home/deck/Projects/glfw-plus/deps /home/deck/Projects/glfw-go/v3.5/glfw/glfw/
cp -r /home/deck/Projects/glfw-plus/include /home/deck/Projects/glfw-go/v3.5/glfw/glfw/
sudo cp -r /home/deck/Projects/glfw-go/v3.5/glfw /usr/local/go/src/
cp -r /home/deck/Projects/glfw-go/v3.5/glfw /home/deck/data/ubuntu-20.04-root/usr/local/go/src/
sudo rm /usr/local/go/src/glfw/build_cgo_hack.go
rm /home/deck/data/ubuntu-20.04-root/usr/local/go/src/glfw/build_cgo_hack.go