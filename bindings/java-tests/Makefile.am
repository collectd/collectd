EXTRA_DIST = org/collectd/java

CLASSPATH=.:libs/junit-4.12.jar:libs/hamcrest-core-1.3.jar

tests=org.collectd.java.GenericJMXConfValueTest

java-build-stamp:
	$(JAVAC) -d "." "../java/org/collectd/api"/*.java
	$(JAVAC) -d "." "../java/org/collectd/java"/*.java
	$(JAVAC) -cp $(CLASSPATH) -d "." "./org/collectd/java"/*.java
	touch "$@"

check: java-build-stamp
	java -cp $(CLASSPATH) org.junit.runner.JUnitCore $(tests)

all-local: check

install-exec-local:

uninstall-local:

clean-local:
	rm -f "org/collectd/api"/*.class
	rm -f "org/collectd/java"/*.class
	rm -f "java-build-stamp"
