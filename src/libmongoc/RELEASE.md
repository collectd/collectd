# RELEASE PROCESS

Version/release numbers
-----------------------

docs/source/sphinx/source

    building.rst

        to build version ...
        git checkout ...

    conf.py

        version = ...
        release = ...

    index.rst

doxygenConfig

    PROJECT_NUMBER = ...

Makefile

    MONGO_MAJOR=...
    MONGO_MINOR=...
    MONGO_PATCH=...

SConstruct

    MAJOR_VERSION = ...
    MINOR_VERSION = ...
    PATCH_VERSION = ...

build docs
----------

scons docs

web pages
---------

http://api.mongodb.org/c/current/



http://www.mongodb.org/display/DOCS/C+Language+Center

    You can download the latest stable version: ...


