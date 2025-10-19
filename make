#!/usr/bin/env python3

from ml import build
from ml.boilerplate import cpp
import sys
import os
import shutil
sys.path.append("../")

cpp.generate("../src")
cpp.generate("../../../frameworks")

fm = "/media/romain/Donnees/Programmation/cpp/frameworks"
libs = "/media/romain/Donnees/Programmation/cpp/libs"

for arg in sys.argv:
    if "libs=" in arg:
        libs = arg.split("=")[1]
    elif "fm=" in arg or "cpp-utils=" in arg:
        fm = arg.split("=")[1]

includes = [
    "../src",
    fm,
    libs + "/json",
    libs + "/eigen",
    ]

srcs = [
        "../src",
        ]

b = build.create("fxhub", sys.argv)
b.includes = includes
b.addToSrcs(srcs)

b.addToLibs([
    "stdc++fs",
    "boost_filesystem",
    ])
#b.definitions += ["NO_LOG"]

if not b.release : 
    b.addToLibs([
        fm + "/build/libmlapi.so",
    ])
else : 
    b.addProject([
        "/opt/mlapi/lib",
        ])

if ("clean" in sys.argv or "clear" in sys.argv) :
    b.clean()
else :
    b.build()
