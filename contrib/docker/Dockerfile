FROM debian:stable

ENV DEBIAN_FRONTEND noninteractive
COPY 50docker-apt-conf /etc/apt/apt.conf.d/

COPY rootfs_prefix/ /usr/src/rootfs_prefix/

RUN apt-get update \
 && apt-get upgrade \
 && apt-get install \
    collectd-core \
    collectd-utils \
    build-essential \
 && make -C /usr/src/rootfs_prefix/ \
 && apt-get --purge remove build-essential \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

RUN mkdir /etc/collectd/collectd.conf.d
COPY collectd.conf /etc/collectd/collectd.conf

ENV LD_PRELOAD /usr/src/rootfs_prefix/rootfs_prefix.so

CMD [ "/usr/sbin/collectd", "-f"]
