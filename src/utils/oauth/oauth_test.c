/**
 * collectd - src/tests/utils_oauth_test.c
 * Copyright (C) 2015  Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at google.com>
 **/

#include "testing.h"
#include "utils/oauth/oauth.h"

struct {
  char *json;
  int status;
  char *access_token;
  cdtime_t expires_in;
} cases[] = {
    {
        "{\"access_token\":\"MaeC6kaePhie1ree\",\"expires_in\":3600}",
        /* status = */ 0, "MaeC6kaePhie1ree", TIME_T_TO_CDTIME_T_STATIC(3600),
    },
    {
        "{\"token_type\":\"Bearer\",\"expires_in\":1800,\"access_token\":"
        "\"aeThiebee2gushuY\"}",
        /* status = */ 0, "aeThiebee2gushuY", TIME_T_TO_CDTIME_T_STATIC(1800),
    },
    {
        "{\"ignored_key\":\"uaph5aewaeghi1Ge\",\"expires_in\":3600}",
        /* status = */ -1, NULL, 0,
    },
    {
        /* expires_in missing */
        "{\"access_token\":\"shaephohbie9Ahch\"}",
        /* status = */ -1, NULL, 0,
    },
};

DEF_TEST(simple) /* {{{ */
{
  size_t i;
  _Bool success = 1;

  for (i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    char buffer[1024];
    cdtime_t expires_in;

    EXPECT_EQ_INT(cases[i].status,
                  oauth_parse_json_token(cases[i].json, buffer, sizeof(buffer),
                                         &expires_in));
    if (cases[i].status != 0)
      continue;

    EXPECT_EQ_STR(cases[i].access_token, buffer);
    EXPECT_EQ_UINT64(cases[i].expires_in, expires_in);
  }

  return success ? 0 : -1;
} /* }}} simple */

DEF_TEST(oauth_create_google_json) {
  char const *in =
      "{\"type\": \"service_account\","
      "\"project_id\":\"collectd.org:unit-test\","
      "\"private_key_id\": \"ed7b4eb6c1b61a7bedab5bcafff374f7fc820698\","
      "\"private_key\":\"-----BEGIN PRIVATE KEY-----\\n"
      "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQDNvS71Lr2WIEqx\\n"
      "U766iJGORVVib0FnHhOf/0FEI4Hw+tF11vP3LZj0AyQFIi/h2l2EDXOr43C6Gt+K\\n"
      "0stsyaWvRNzeQa+dUFY5A/ZEtdvYVPq7KudML5Hs9DNmWFlM/iIfQyIUJ+vHv7fe\\n"
      "pJGgu4ZgSkNWehmWj3qiRzIvYxKvDIQizqPZNlTh+33KQcT2x+ErkuB3snQu8hSK\\n"
      "HAg2sCvORqKGOvN9F4bAqXt5T0NVjGy4YXeuif1p/Np/GH6Ys1p+etgGwvIimXIv\\n"
      "jFL9K/ZtrTOcFdy4R5bwrj2piCZa2T5H6fupVp2tVgIuS53r2fEaBMLD97oAvwZ3\\n"
      "9XPxG1NLAgMBAAECggEACgHroKcrN1FkdgyzSIKFG1evCBCOV17kqHyI5wYXzNTT\\n"
      "zyNrZDjBFGQkt+U0/AucTznnnahSCZNuD+QiBgLRqYgJevwp99Z6YzVDS438Xsuq\\n"
      "Ezmf3O+sGEu78Pys11cTP38LT3yuS4iSqo9Jus5JrTG05dDJoYO4J4rxW3xlDRj8\\n"
      "lQUimXI+S9skaSusf0oErDrjuQG9dxmhnGcSEX+rIe9G0UygTNuI0KKGJ8jmnPz5\\n"
      "OS+sM8qrKcnjrvENFWKLb11HlliHkh6dILoO5rvf5DR+XGKM7BFAsdWg6oI7SFGh\\n"
      "S6zGZ0jUR7QAugrjbTlDOCnAuZ+Mbc/4yHZ3u5PlcQKBgQDuvH1ds1YmmbOllOK5\\n"
      "JtkdjCUUyH1bgkMrmcg/KkRARPRHQvfAioZsC6d0fa6jq0kTW/3Zu14IsVXgM8xK\\n"
      "fuNSp8LdY+NCtJnfvdLaChgAwZaQLX4qgV0qYw8iLv5ifa4ZY0qaZioJCzkv57y1\\n"
      "KkavYvITboO7aUSa441Zko9c+wKBgQDcndg0QpWH6JMz/FkCf/KDyW/cUODfKXhP\\n"
      "5p9eTcVlfDL2sAb2RzVhvKZcuWXVwnfaDP0oBj2/SBLGx0idUb+VHdM/IGiLroyK\\n"
      "pAHpNM//dowiGL1qPPOLXrzF/vn+w4t2Dqggfcqu52SzRiyaxUtSMnNyyyU19cO+\\n"
      "pb7wAS5x8QKBgCW7WL0UeQtEw6Xp8CN/RlVrLvkn7tglsGQVvBZvobXesBULOokN\\n"
      "28z70o2Qx6dKjRQoN+jPuj75eC8lQKaNg3Qu25eOD/8c+CzqnYakjcKg1iEXb5dc\\n"
      "NtNaMKwgbUg3wOp2TPY2K3KeeX1ezO59LgrOQqBbmSpnqtYoHNEJXus9AoGAWl/y\\n"
      "9J2eIdm9i5tBX0vIrgHz5/3d0K1tUtX3zSrwxT0Wp4W+pF7RWGNuhyePtvx+Gn4d\\n"
      "qqq72sMMpg93CLM3Vz+rjP2atjXf7t92xPDUkCMhDsqxtXaYkixSCo4EHUA/vjIM\\n"
      "35qIUBQMZYBGv3Q5AcgXERx09uDhuhSt3iWtwBECgYAHFnCh8fKsJbQrVN10tU/h\\n"
      "ofVx0KZkUpBz8eNQPuxt4aY+LyWsKVKtnduw2WdumuOY66cUN1lsi8Bz/cq1dhPt\\n"
      "Oc2S7pqjbu2Q1Oqx+/yr6jqsvKaSxHmcpbWQBsGn6UaWZgYZcAtQBcqDAp7pylwj\\n"
      "tejRh0NB8d81H5Dli1Qfzw==\\n"
      "-----END PRIVATE KEY-----\\n\","
      "\"client_email\":\"example-sacct@unit-test.iam.gserviceaccount.com\", "
      "\"client_id\": \"109958449193027604084\","
      "\"auth_uri\":\"https://accounts.google.com/o/oauth2/auth\","
      "\"token_uri\":\"https://accounts.google.com/o/oauth2/token\","
      "\"auth_provider_x509_cert_url\":"
      "\"https://www.googleapis.com/oauth2/v1/certs\","
      "\"client_x509_cert_url\":\"https://www.googleapis.com/robot/v1/"
      "metadata/x509/example-sacct%40ssc-serv-dev.iam.gserviceaccount.com\"}";

  oauth_google_t ret =
      oauth_create_google_json(in, "https://collectd.org/example.scope");

  EXPECT_EQ_STR("collectd.org:unit-test", ret.project_id);

  CHECK_NOT_NULL(ret.oauth);
  struct {
    char *url;
    char *iss;
    char *aud;
    char *scope;
  } *obj = (void *)ret.oauth;

  EXPECT_EQ_STR("https://accounts.google.com/o/oauth2/token", obj->url);
  EXPECT_EQ_STR("example-sacct@unit-test.iam.gserviceaccount.com", obj->iss);
  EXPECT_EQ_STR("https://collectd.org/example.scope", obj->scope);

  free(ret.project_id);
  oauth_destroy(ret.oauth);

  return 0;
}

int main(int argc, char **argv) /* {{{ */
{
  RUN_TEST(simple);
  RUN_TEST(oauth_create_google_json);

  END_TEST;
} /* }}} int main */
