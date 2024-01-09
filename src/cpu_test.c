/**
 * collectd - src/cpu_test.c
 * Copyright (C) 2024      Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "cpu.c" /* sic */
#include "testing.h"

DEF_TEST(usage_rate) {
  usage_t usage = {0};

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  derive_t count_t0 = 3000;

  usage_init(&usage, t0);
  usage_record(&usage, 0, STATE_USER, count_t0);

  // Unable to calculate a rate with a single data point.
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_USER));
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_ACTIVE));

  cdtime_t t1 = t0 + TIME_T_TO_CDTIME_T(10);
  derive_t count_t1 = count_t0 + 100;
  gauge_t want_rate = 100.0 / 10.0;

  usage_init(&usage, t1);
  usage_record(&usage, 0, STATE_USER, count_t1);

  EXPECT_EQ_DOUBLE(want_rate, usage_rate(&usage, 0, STATE_USER));
  EXPECT_EQ_DOUBLE(want_rate, usage_rate(&usage, 0, STATE_ACTIVE));

  // States that we have not set should be NAN
  for (state_t s = 0; s < STATE_ACTIVE; s++) {
    if (s == STATE_USER) {
      continue;
    }
    EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, s));
  }

  usage_reset(&usage);
  return 0;
}

DEF_TEST(usage_ratio) {
  usage_t usage = {0};

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  usage_init(&usage, t0);
  for (size_t cpu = 0; cpu < 4; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      usage_record(&usage, cpu, s, 1000);
    }
  }

  cdtime_t t1 = t0 + TIME_T_TO_CDTIME_T(10);
  usage_init(&usage, t1);
  derive_t global_increment = 0;
  for (size_t cpu = 0; cpu < 4; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      global_increment += increment;
      usage_record(&usage, cpu, s, 1000 + increment);
    }
  }

  for (size_t cpu = 0; cpu < 4; cpu++) {
    derive_t active_increment = 0;
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      if (s != STATE_IDLE) {
        active_increment += increment;
      }
      gauge_t want_ratio = ((gauge_t)increment) / ((gauge_t)global_increment);
      EXPECT_EQ_DOUBLE(want_ratio, usage_ratio(&usage, cpu, s));
    }
    gauge_t want_active_ratio =
        ((gauge_t)active_increment) / ((gauge_t)global_increment);
    EXPECT_EQ_DOUBLE(want_active_ratio, usage_ratio(&usage, cpu, STATE_ACTIVE));
  }

  gauge_t sum = 0;
  for (size_t cpu = 0; cpu < 4; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      gauge_t rate = usage_ratio(&usage, cpu, s);
      sum += rate;
    }
  }
  EXPECT_EQ_DOUBLE(1.0, sum);

  usage_reset(&usage);
  return 0;
}

static bool expect_usage_count(derive_t want, derive_t got, size_t cpu,
                               state_t state) {
  char prefix[510] = "usage_global_count(";
  if (cpu != SIZE_MAX) {
    snprintf(prefix, sizeof(prefix), "usage_count(cpu=%zu, ", cpu);
  }

  bool ok = true;
  char msg[1024] = {0};
  snprintf(msg, sizeof(msg), "%sstate=\"%s\") = %" PRId64, prefix,
           cpu_state_names[state], got);

  derive_t diff = got - want;
  if (diff < -1 || diff > 1) {
    snprintf(msg, sizeof(msg), "%sstate=\"%s\") = %" PRId64 ", want %" PRId64,
             prefix, cpu_state_names[state], got, want);
    ok = false;
  }

  LOG(ok, msg);
  return ok;
}

DEF_TEST(usage_count) {
  int ret = 0;
  usage_t usage = {0};
#define CPU_NUM 2

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  usage_init(&usage, t0);
  for (size_t cpu = 0; cpu < CPU_NUM; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      usage_record(&usage, cpu, s, 1000);
    }
  }
  usage_finalize(&usage);

  cdtime_t interval = TIME_T_TO_CDTIME_T(300);
  cdtime_t t1 = t0 + interval;
  usage_init(&usage, t1);
  derive_t cpu_increment[CPU_NUM] = {0};
  for (size_t cpu = 0; cpu < CPU_NUM; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      cpu_increment[cpu] += increment;
      usage_record(&usage, cpu, s, 1000 + increment);
    }
  }

  gauge_t sum_time = 0;
  for (size_t cpu = 0; cpu < CPU_NUM; cpu++) {
    derive_t active_increment = 0;
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      if (s != STATE_IDLE) {
        active_increment += increment;
      }

      gauge_t want_time = 1000000.0 * CDTIME_T_TO_DOUBLE(interval) *
                          ((gauge_t)increment) / ((gauge_t)cpu_increment[cpu]);
      sum_time += want_time;

      bool ok = expect_usage_count((derive_t)want_time,
                                   usage_count(&usage, cpu, s), cpu, s);
      ret = ret || !ok;
    }

    gauge_t want_active_time = 1000000.0 * CDTIME_T_TO_DOUBLE(interval) *
                               ((gauge_t)active_increment) /
                               ((gauge_t)cpu_increment[cpu]);
    bool ok = expect_usage_count((derive_t)want_active_time,
                                 usage_count(&usage, cpu, STATE_ACTIVE), cpu,
                                 STATE_ACTIVE);
    ret = ret || !ok;
  }
  EXPECT_EQ_DOUBLE(CPU_NUM * 1000000.0 * CDTIME_T_TO_DOUBLE(interval),
                   sum_time);

  usage_reset(&usage);
  return ret;
}

DEF_TEST(usage_active_rate) {
  usage_t usage = {0};

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  derive_t user_t0 = 1000;
  derive_t syst_t0 = 2000;
  derive_t idle_t0 = 3000;

  usage_init(&usage, t0);
  usage_record(&usage, 0, STATE_USER, user_t0);
  usage_record(&usage, 0, STATE_SYSTEM, syst_t0);
  usage_record(&usage, 0, STATE_IDLE, idle_t0);

  // Unable to calculate a rate with a single data point.
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_USER));
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_SYSTEM));
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_IDLE));
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_ACTIVE));
  EXPECT_EQ_DOUBLE(NAN, usage_active_rate(&usage, 0));

  cdtime_t t1 = t0 + TIME_T_TO_CDTIME_T(10);
  derive_t user_t1 = user_t0 + 200;
  derive_t syst_t1 = syst_t0 + 100;
  derive_t idle_t1 = idle_t0 + 700;

  gauge_t want_user_rate = 200.0 / 10.0;
  gauge_t want_syst_rate = 100.0 / 10.0;
  gauge_t want_idle_rate = 700.0 / 10.0;
  gauge_t want_active_rate = want_user_rate + want_syst_rate;

  usage_init(&usage, t1);
  usage_record(&usage, 0, STATE_USER, user_t1);
  usage_record(&usage, 0, STATE_SYSTEM, syst_t1);
  usage_record(&usage, 0, STATE_IDLE, idle_t1);

  EXPECT_EQ_DOUBLE(want_user_rate, usage_rate(&usage, 0, STATE_USER));
  EXPECT_EQ_DOUBLE(want_syst_rate, usage_rate(&usage, 0, STATE_SYSTEM));
  EXPECT_EQ_DOUBLE(want_idle_rate, usage_rate(&usage, 0, STATE_IDLE));
  EXPECT_EQ_DOUBLE(want_active_rate, usage_rate(&usage, 0, STATE_ACTIVE));
  EXPECT_EQ_DOUBLE(want_active_rate, usage_active_rate(&usage, 0));

  usage_reset(&usage);
  return 0;
}

DEF_TEST(usage_global_rate) {
  usage_t usage = {0};

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  derive_t cpu0_t0 = 1000;
  derive_t cpu1_t0 = 2000;

  usage_init(&usage, t0);
  usage_record(&usage, 0, STATE_USER, cpu0_t0);
  usage_record(&usage, 1, STATE_USER, cpu1_t0);

  // Unable to calculate a rate with a single data point.
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 0, STATE_USER));
  EXPECT_EQ_DOUBLE(NAN, usage_rate(&usage, 1, STATE_USER));
  EXPECT_EQ_DOUBLE(NAN, usage_global_rate(&usage, STATE_USER));
  EXPECT_EQ_DOUBLE(NAN, usage_global_rate(&usage, STATE_ACTIVE));

  cdtime_t t1 = t0 + TIME_T_TO_CDTIME_T(10);
  derive_t cpu0_t1 = cpu0_t0 + 300;
  derive_t cpu1_t1 = cpu1_t0 + 700;

  gauge_t want_cpu0_rate = 300.0 / 10.0;
  gauge_t want_cpu1_rate = 700.0 / 10.0;
  gauge_t want_global_rate = want_cpu0_rate + want_cpu1_rate;

  usage_init(&usage, t1);
  usage_record(&usage, 0, STATE_USER, cpu0_t1);
  usage_record(&usage, 1, STATE_USER, cpu1_t1);

  EXPECT_EQ_DOUBLE(want_cpu0_rate, usage_rate(&usage, 0, STATE_USER));
  EXPECT_EQ_DOUBLE(want_cpu1_rate, usage_rate(&usage, 1, STATE_USER));
  EXPECT_EQ_DOUBLE(want_global_rate, usage_global_rate(&usage, STATE_USER));
  EXPECT_EQ_DOUBLE(want_global_rate, usage_global_rate(&usage, STATE_ACTIVE));

  usage_reset(&usage);
  return 0;
}

DEF_TEST(usage_global_ratio) {
  usage_t usage = {0};

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  usage_init(&usage, t0);
  for (size_t cpu = 0; cpu < 4; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      usage_record(&usage, cpu, s, 1000);
    }
  }

  cdtime_t t1 = t0 + TIME_T_TO_CDTIME_T(10);
  usage_init(&usage, t1);
  derive_t global_increment = 0;
  for (size_t cpu = 0; cpu < 4; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      global_increment += increment;
      usage_record(&usage, cpu, s, 1000 + increment);
    }
  }

  derive_t global_active_increment = 0;
  for (state_t s = 0; s < STATE_ACTIVE; s++) {
    derive_t state_increment = 0;
    for (size_t cpu = 0; cpu < 4; cpu++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      state_increment += increment;
    }
    gauge_t want_state_ratio =
        ((gauge_t)state_increment) / ((gauge_t)global_increment);
    EXPECT_EQ_DOUBLE(want_state_ratio, usage_global_ratio(&usage, s));

    if (s != STATE_IDLE) {
      global_active_increment += state_increment;
    }
  }
  gauge_t want_global_active_ratio =
      ((gauge_t)global_active_increment) / ((gauge_t)global_increment);
  EXPECT_EQ_DOUBLE(want_global_active_ratio,
                   usage_global_ratio(&usage, STATE_ACTIVE));

  EXPECT_EQ_DOUBLE(1.0 - want_global_active_ratio,
                   usage_global_ratio(&usage, STATE_IDLE));

  usage_reset(&usage);
  return 0;
}

/* sumup returns the sum of 1 + 2 + ... + n */
static derive_t sumup(derive_t n) { return n * (n + 1) / 2; }

DEF_TEST(usage_global_count) {
  int ret = 0;
  usage_t usage = {0};
#define CPU_NUM 2

  cdtime_t t0 = TIME_T_TO_CDTIME_T(100);
  usage_init(&usage, t0);
  for (size_t cpu = 0; cpu < CPU_NUM; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      usage_record(&usage, cpu, s, 1000);
    }
  }
  usage_finalize(&usage);

  cdtime_t interval = TIME_T_TO_CDTIME_T(300);

  cdtime_t t1 = t0 + interval;
  usage_init(&usage, t1);
  for (size_t cpu = 0; cpu < CPU_NUM; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      usage_record(&usage, cpu, s, 1000 + increment);
    }
  }

  derive_t cpu_increment[CPU_NUM] = {
      sumup(10),
      sumup(21) - sumup(10),
  };

  gauge_t state_ratio[STATE_MAX] = {0};
  for (size_t cpu = 0; cpu < CPU_NUM; cpu++) {
    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      derive_t increment = ((derive_t)cpu * STATE_ACTIVE) + ((derive_t)s);
      gauge_t ratio = ((gauge_t)increment) / ((gauge_t)cpu_increment[cpu]);

      state_ratio[s] += ratio;
      if (s != STATE_IDLE) {
        state_ratio[STATE_ACTIVE] += ratio;
      }
    }
  }

  for (state_t s = 0; s < STATE_MAX; s++) {
    gauge_t want_time =
        1000000.0 * CDTIME_T_TO_DOUBLE(interval) * state_ratio[s];
    bool ok = expect_usage_count((derive_t)want_time,
                                 usage_global_count(&usage, s), SIZE_MAX, s);
    ret = ret || !ok;
  }

  gauge_t sum_ratio = 0;
  for (state_t s = 0; s < STATE_ACTIVE; s++) {
    sum_ratio += state_ratio[s];
  }
  EXPECT_EQ_DOUBLE(CPU_NUM, sum_ratio);

  usage_reset(&usage);
  return ret;
}

int main(void) {
  RUN_TEST(usage_rate);
  RUN_TEST(usage_ratio);
  RUN_TEST(usage_count);
  RUN_TEST(usage_active_rate);
  RUN_TEST(usage_global_rate);
  RUN_TEST(usage_global_ratio);
  RUN_TEST(usage_global_count);

  END_TEST;
}
