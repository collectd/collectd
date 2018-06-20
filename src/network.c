diff -Naur src/network.c src/network.c
--- src/network.c	2018-06-14 06:29:51.000000000 +0300
+++ src/network.c	2018-06-19 21:27:22.432241000 +0300
@@ -113,6 +113,7 @@
 #endif
   cdtime_t next_resolve_reconnect;
   cdtime_t resolve_interval;
+  struct sockaddr_storage *bind_addr;
 };
 
 struct sockent_server {
@@ -1501,6 +1502,7 @@
     sec->fd = -1;
   }
   sfree(sec->addr);
+  sfree(sec->bind_addr);
 #if HAVE_GCRYPT_H
   sfree(sec->username);
   sfree(sec->password);
@@ -1683,6 +1685,35 @@
   return 0;
 } /* }}} network_set_interface */
 
+static int network_bind_socket_to_addr(sockent_t *se, const struct addrinfo *ai) {
+
+  if (se->data.client.bind_addr == NULL)
+    return 0;
+
+  DEBUG("network_plugin: fd %i: bind socket to address", se->data.client.fd);
+  char pbuffer[64];
+
+  if (ai->ai_family == AF_INET) {
+    struct sockaddr_in *addr = (struct sockaddr_in *)(se->data.client.bind_addr);
+    inet_ntop(AF_INET, &(addr->sin_addr), pbuffer, 64);
+    DEBUG("network_plugin: binding client socket to ipv4 address: %s", pbuffer);
+    if (bind(se->data.client.fd, (struct sockaddr*)addr, sizeof(*addr)) == -1) {
+      ERROR("network_plugin: failed to bind client socket (ipv4): %s", STRERRNO);
+      return -1;
+    }
+  } else if (ai->ai_family == AF_INET6) {
+    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)(se->data.client.bind_addr);
+    inet_ntop(AF_INET, &(addr->sin6_addr), pbuffer, 64);
+    DEBUG("network_plugin: binding client socket to ipv6 address: %s", pbuffer);
+    if (bind(se->data.client.fd, (struct sockaddr*)addr, sizeof(*addr)) == -1) {
+      ERROR("network_plugin: failed to bind client socket (ipv6): %s", STRERRNO);
+      return -1;
+    }
+  }
+
+  return 0;
+} /* int network_bind_socket_to_addr */
+
 static int network_bind_socket(int fd, const struct addrinfo *ai,
                                const int interface_idx) {
 #if KERNEL_SOLARIS
@@ -1834,6 +1865,7 @@
   } else {
     se->data.client.fd = -1;
     se->data.client.addr = NULL;
+    se->data.client.bind_addr = NULL;
     se->data.client.resolve_interval = 0;
     se->data.client.next_resolve_reconnect = 0;
 #if HAVE_GCRYPT_H
@@ -1989,6 +2021,7 @@
 
     network_set_ttl(se, ai_ptr);
     network_set_interface(se, ai_ptr);
+    network_bind_socket_to_addr(se, ai_ptr);
 
     /* We don't open more than one write-socket per
      * node/service pair.. */
@@ -2684,6 +2717,48 @@
   return 0;
 } /* }}} int network_config_set_interface */
 
+static int network_config_set_bind_address(const oconfig_item_t *ci,
+                                        struct sockaddr_storage **bind_address) {
+  if ((*bind_address) != NULL) {
+      ERROR("network_plugin: only a single bind address is allowed");
+      return -1;    
+  }
+
+  char addr_text[256];
+
+  if (cf_util_get_string_buffer(ci, addr_text, sizeof(addr_text)) != 0)
+    return -1;
+
+  int ret;
+  struct addrinfo hint, *res = NULL;
+
+  memset(&hint, '\0', sizeof hint);
+  hint.ai_family = PF_UNSPEC;
+  hint.ai_flags = AI_NUMERICHOST;
+
+  ret = getaddrinfo(addr_text, NULL, &hint, &res);
+  if (ret) {
+    ERROR("network plugin: Bind address option has invalid address set: %s", gai_strerror(ret));
+    return -1;
+  }
+
+  *bind_address = malloc(sizeof(**bind_address));
+  (*bind_address)->ss_family = res->ai_family;
+  if(res->ai_family == AF_INET) {
+    struct sockaddr_in* addr = (struct sockaddr_in*)(*bind_address);
+    inet_pton(AF_INET, addr_text, &(addr->sin_addr));
+  } else if (res->ai_family == AF_INET6) {
+    struct sockaddr_in6* addr = (struct sockaddr_in6*)(*bind_address);
+    inet_pton(AF_INET6, addr_text, &(addr->sin6_addr));
+  } else {
+    ERROR("network plugin: %s is an unknown address format %d\n",addr_text,res->ai_family);
+    sfree(*bind_address);
+    return -1;
+  }
+
+  return 0;
+} /* int network_config_set_bind_address */
+
 static int network_config_set_buffer_size(const oconfig_item_t *ci) /* {{{ */
 {
   int tmp = 0;
@@ -2843,6 +2918,8 @@
 #endif /* HAVE_GCRYPT_H */
         if (strcasecmp("Interface", child->key) == 0)
       network_config_set_interface(child, &se->interface);
+    else if (strcasecmp("BindAddress", child->key) == 0)
+      network_config_set_bind_address(child, &se->data.client.bind_addr);
     else if (strcasecmp("ResolveInterval", child->key) == 0)
       cf_util_get_cdtime(child, &se->data.client.resolve_interval);
     else {
