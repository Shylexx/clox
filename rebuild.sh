#!/bin/bash
rm -rf ./build/*
cmake -S ./ -B ./build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=1
