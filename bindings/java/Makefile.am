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
	     org/collectd/api/ValueList.java

java-build-stamp: org/collectd/api/*.java
	$(JAVAC) -d "." "$(srcdir)/org/collectd/api"/*.java
	touch "$@"

all-local: java-build-stamp

install-exec-local: java-build-stamp
	mkdir -p "$(pkgdatadir)/java/org/collectd/api"
	$(INSTALL) -m 644 "org/collectd/api"/*.class \
		"$(DESTDIR)$(pkgdatadir)/java/org/collectd/api/"

clean-local:
	rm -f "org/collectd/api"/*.class
	rm -f "java-build-stamp"
