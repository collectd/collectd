package org.collectd.java;

import org.collectd.api.OConfigItem;
import org.junit.Before;
import org.junit.Test;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import static org.collectd.api.DataSource.TYPE_DERIVE;
import static org.hamcrest.CoreMatchers.is;
import static org.junit.Assert.assertThat;

public class GenericJMXConfValueTest {

    private GenericJMXConfValue confValue;

    @Before
    public void setUp() throws Exception {
        final OConfigItem config = new OConfigItem("");
        final OConfigItem type = new OConfigItem("Type");
        type.addValue("DERIVE");
        config.addChild(type);
        final OConfigItem attribute = new OConfigItem("Attribute");
        attribute.addValue("test");
        config.addChild(attribute);
        confValue = new GenericJMXConfValue(config);
    }

    @Test
    public void genericObjectToNumber_should_copy_value_from_AtomicLong() {
        final AtomicLong originalAtomicLong = new AtomicLong(2L);
        final Number result = confValue.genericObjectToNumber(originalAtomicLong, TYPE_DERIVE);
        originalAtomicLong.set(3L);

        assertThat((Long) result, is(2L));
    }

    @Test
    public void genericObjectToNumber_should_copy_value_from_AtomicInteger() {
        final AtomicInteger originalAtomicInteger = new AtomicInteger(4);
        final Number result = confValue.genericObjectToNumber(originalAtomicInteger, TYPE_DERIVE);
        originalAtomicInteger.set(5);

        assertThat((Integer) result, is(4));
    }
}