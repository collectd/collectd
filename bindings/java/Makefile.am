EXTRA_DIST = org/collectd/api/CollectdConfigInterface.java \
	     org/collectd/api/CollectdFlushInterface.java \
	     org/collectd/api/CollectdInitInterface.java \
	     org/collectd/api/Collectd.java \
	     org/collectd/api/CollectdLogInterface.java \
	     org/collectd/api/CollectdMatchFactoryInterface.java \
	     org/collectd/api/CollectdMatchInterface.java \
	     org/collectd/api/CollectdNotificationInterface.java \
	     org/collectd/api/CollectdReadInterface.java \
	     org/collectd/api/CollectdShutdownInterface.java \
	     org/collectd/api/CollectdTargetFactoryInterface.java \
	     org/collectd/api/CollectdTargetInterface.java \
	     org/collectd/api/CollectdWriteInterface.java \
	     org/collectd/api/DataSet.java \
	     org/collectd/api/DataSource.java \
	     org/collectd/api/Notification.java \
	     org/collectd/api/OConfigItem.java \
	     org/collectd/api/OConfigValue.java \
	     org/collectd/api/PluginData.java \
	     org/collectd/api/ValueList.java \
	     org/collectd/java/GenericJMXConfConnection.java \
	     org/collectd/java/GenericJMXConfMBean.java \
	     org/collectd/java/GenericJMXConfValue.java \
	     org/collectd/java/GenericJMX.java \
	     org/collectd/java/JMXMemory.java

java-build-stamp: org/collectd/api/*.java org/collectd/java/*.java
	$(JAVAC) -d "." "$(srcdir)/org/collectd/api"/*.java
	$(JAVAC) -d "." "$(srcdir)/org/collectd/java"/*.java
	mkdir -p .libs
	$(JAR) cf .libs/collectd-api.jar "org/collectd/api"/*.class
	$(JAR) cf .libs/generic-jmx.jar "org/collectd/java"/*.class
	touch "$@"

all-local: java-build-stamp

install-exec-local: java-build-stamp
	mkdir -p "$(DESTDIR)$(pkgdatadir)/java"
	$(INSTALL) -m 644 .libs/collectd-api.jar \
		"$(DESTDIR)$(pkgdatadir)/java"
	$(INSTALL) -m 644 .libs/generic-jmx.jar \
		"$(DESTDIR)$(pkgdatadir)/java"

clean-local:
	rm -f "org/collectd/api"/*.class
	rm -f "org/collectd/java"/*.class
	rm -f .libs
	rm -f "java-build-stamp"
