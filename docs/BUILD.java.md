# Building with Java

This file gives some background and hints how the *java plugin* needs to be
configured.

## Dependencies

The *java plugin* requires a version of Java with *Java Native Interface* (JNI)
**1.2** or later.

## Configure and flags

To determine the location of the required files of a Java installation is not an
easy task, because the locations vary with your kernel (Linux, SunOS, …) and
with your architecture (x86, SPARC, …) and there is no `java-config` script we
could use. Configuration of the JVM library is therefore a bit tricky.

The easiest way to use the `--with-java="${JAVA_HOME}"` option, where
`JAVA_HOME` is usually something like:

    /usr/lib/jvm/java-1.5.0-sun-1.5.0.14

The configure script will then use *find(1)* to look for the following files:

 *  `jni.h`
 *  `jni_md.h`
 *  `libjvm.so`

If found, appropriate CPP-flags and LD-flags are set and the following library
checks succeed.

If this doesn't work for you, you have the possibility to specify CPP-flags,
C-flags, LD-flags and LIBS for the *java plugin* by hand, using the following
environment variables:

 *  `JAVA_CPPFLAGS`
 *  `JAVA_CFLAGS`
 *  `JAVA_LDFLAGS`
 *  `JAVA_LIBS`

For example (shortened for demonstration purposes):

    ./configure JAVA_CPPFLAGS="-I$JAVA_HOME/include -I$JAVA_HOME/include/linux"

Adding `-ljvm` to JAVA_LIBS is done automatically, you don't have to do that.

## License

The *java plugin* is licensed under the *GNU General Public License, version 2*.
Full licensing terms can be found in the file `COPYING`.
