/**
 * Copyright (C) 2024 Kentaro Hayashi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Kentaro Hayashi <kenhys at xdump.org>
 **/

#include "collectd.h"
#include "lua.c"
#include "testing.h"
#include "utils_lua.c"

static char config_path[PATH_MAX] = {0};
const char *module_key_value_config = "<Plugin lua>\n"
                                      "  Script \"example.lua\"\n"
                                      "  <Module>\n"
                                      "    Key \"Value\"\n"
                                      "  </Module>\n"
                                      "</Plugin>\n";

void test_setup_config(const char *config) {
  int fd = 0;
  FILE *fp = NULL;
  {
    /* for simplicity, use lua.conf.XXXXXX here instead of .conf */
    strncpy(config_path, "lua.conf.XXXXXX", sizeof(config_path));
    fd = mkstemp(config_path);
    fp = fdopen(fd, "w+");
    fprintf(fp, "%s", config);
    fclose(fp);
  }
  {
    /* create empty Lua script which is loaded via lua_config */
    fp = fopen("example.lua", "w+");
    fclose(fp);
  }
}

void test_teardown_config(void) {
  lua_close(scripts->lua_state);
  unlink(config_path);
  unlink("example.lua");
}

lua_State *setup_lua(void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  return L;
}

void teardown_lua(lua_State *L) { lua_close(L); }

DEF_TEST(lua_config) {
  test_setup_config(module_key_value_config);

  oconfig_item_t *root = oconfig_parse_file(config_path);
  oconfig_item_t *plugin = root->children;

  /*
    lua_config internally calls luaC_pushoconfigitem
    and lua_State context is stored in each script.
    <Module> will be mapped to Lua table:
    { Key => Value } so check the value of "Key".
  */
  lua_config(plugin);
  lua_State *L = scripts->lua_state;
  lua_getglobal(L, "example.lua");
  lua_getfield(L, -1, "Key");
  EXPECT_EQ_STR("Value", luaL_checkstring(L, -1));

  oconfig_free(root);
  test_teardown_config();
  return 0;
}

DEF_TEST(luaC_pushnotification) {
  notification_t notify = {.severity = NOTIF_OKAY, .plugin = "lua"};

  lua_State *L = setup_lua();
  /*
    notification_t will be mapped to the following Lua table:

    { severity => "okay", , plugin => "lua" }
  */
  luaC_pushnotification(L, &notify);
  lua_getfield(L, -1, "severity");
  lua_getfield(L, -2, "plugin");
  EXPECT_EQ_STR("lua", luaL_checkstring(L, -1));
  EXPECT_EQ_STR("okay", luaL_checkstring(L, -2));

  teardown_lua(L);
  return 0;
}

int main() {
  RUN_TEST(lua_config);
  RUN_TEST(luaC_pushnotification);
  END_TEST;
}
