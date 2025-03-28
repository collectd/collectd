#include <arrow/io/memory.h>
#include <parquet/arrow/reader.h>
#include <parquet/stream_reader.h>
#include <thread>

#define MOCK_OPEN_FILE

#include "write_parquet.cc"

#undef MOCK_OPEN_FILE

extern "C" {
#include "math.h"
#include "testing.h"

#include "daemon/metric.h"
#include "daemon/utils_time.h"
#include "utils/common/common.h"
#include "utils/strbuf/strbuf.h"
}

static int check_result(int size, char type,
                        std::shared_ptr<arrow::Buffer> buffer) {
  auto reader = parquet::StreamReader{parquet::ParquetFileReader::Open(
      std::make_shared<arrow::io::BufferReader>(buffer))};

  int64_t value_int;
  double value_double;
  for (int i = 0; i < size; i++) {
    if (type == 'i') {
      reader >> value_int;
      EXPECT_EQ_INT(i, value_int);
    } else {
      reader >> value_double;
      EXPECT_EQ_DOUBLE(i + 0.0, value_double);
    }
    reader >> parquet::EndRow;
  }
  return 0;
}

static std::shared_ptr<arrow::Buffer> get_buffer(char type, IWriter &writer) {
  if (type == 'i') {
    auto &real_writer = dynamic_cast<Writer<int64_t> &>(writer);
    return std::static_pointer_cast<arrow::io::BufferOutputStream>(
               real_writer.file.stream())
        ->Finish()
        .ValueOrDie();
  }
  auto &real_writer = dynamic_cast<Writer<double> &>(writer);
  return std::static_pointer_cast<arrow::io::BufferOutputStream>(
             real_writer.file.stream())
      ->Finish()
      .ValueOrDie();
}

static std::string ms_to_string(uint64_t ms, std::string &pattern) {
  (void)pattern;
  return wp_time_point_to_string(std::chrono::system_clock::from_time_t(
                                     CDTIME_T_TO_TIME_T(MS_TO_CDTIME_T(ms))),
                                 pattern);
}

DEF_TEST(time_formating) {
  std::string pattern = "%Y %m %d %H:%M:%S";

  std::string data =
      ms_to_string(1000ULL * 365 * 24 * 3600 * 50 - 3 * 1000 * 3600, pattern);
  EXPECT_EQ_STR("2019 12 20 00:00:00", data.c_str());

  data = ms_to_string(
      1000ULL * 365 * 24 * 3600 * 54 + 3 * 1000 + 14 * 60 * 1000, pattern);
  EXPECT_EQ_STR("2023 12 19 03:14:03", data.c_str());

  data =
      ms_to_string(1000ULL * 365 * 24 * 3600 * 55 +
                       2ULL * 31 * 24 * 3600 * 1000 + 11ULL * 24 * 3600 * 1000,
                   pattern);
  EXPECT_EQ_STR("2025 03 01 03:00:00", data.c_str());

  return 0;
}

DEF_TEST(parse_metric) {
  metric_family_t *family =
      (metric_family_t *)calloc(1, sizeof(metric_family_t));
  metric_t *mt = (metric_t *)calloc(1, sizeof(metric_t));
  mt->family = family;

  mt->value = value_t{.counter = 5};
  family->type = METRIC_TYPE_COUNTER;
  EXPECT_EQ_INT(MetricValueType::INT64, wp_get_metric_type(mt));
  EXPECT_EQ_INT(5, wp_parse_metric_int(mt));

  mt->value = value_t{.up_down = 11};
  family->type = METRIC_TYPE_UP_DOWN;
  EXPECT_EQ_INT(MetricValueType::INT64, wp_get_metric_type(mt));
  EXPECT_EQ_INT(11, wp_parse_metric_int(mt));

  mt->value = value_t{.gauge = 42.1};
  family->type = METRIC_TYPE_GAUGE;
  EXPECT_EQ_INT(MetricValueType::DOUBLE, wp_get_metric_type(mt));
  EXPECT_EQ_DOUBLE(42.1, wp_parse_metric_double(mt));

  mt->value = value_t{.counter_fp = 100.9};
  family->type = METRIC_TYPE_COUNTER_FP;
  EXPECT_EQ_INT(MetricValueType::DOUBLE, wp_get_metric_type(mt));
  EXPECT_EQ_DOUBLE(100.9, wp_parse_metric_double(mt));

  mt->value = value_t{.up_down_fp = 54321.012345};
  family->type = METRIC_TYPE_UP_DOWN_FP;
  EXPECT_EQ_INT(MetricValueType::DOUBLE, wp_get_metric_type(mt));
  EXPECT_EQ_DOUBLE(54321.012345, wp_parse_metric_double(mt));

  family->type = METRIC_TYPE_UNTYPED;
  // `nan` value stored in double
  EXPECT_EQ_INT(MetricValueType::DOUBLE, wp_get_metric_type(mt));
  EXPECT_EQ_DOUBLE(NAN, wp_parse_metric_double(mt));

  free(mt);
  free(family);
  return 0;
}

DEF_TEST(config_invalid) {
  int res = wp_config_callback("nonExistentOption", "Value");
  EXPECT_EQ_INT(-EINVAL, res);

  res = wp_config_callback("fileduration", "Value");
  EXPECT_EQ_INT(0, res);
  res = wp_init_callback();
  EXPECT_EQ_INT(EINVAL, res);

  res = wp_config_callback("fileduration", "900");
  EXPECT_EQ_INT(0, res);
  res = wp_config_callback("bufferduration", "1900");
  EXPECT_EQ_INT(0, res);
  res = wp_init_callback();
  EXPECT_EQ_INT(EINVAL, res);

  res = wp_config_callback("compression", "Integer");
  EXPECT_EQ_INT(EINVAL, res);

  return 0;
}

DEF_TEST(config_correct) {
  int res = wp_config_callback("fileduration", "7000");
  EXPECT_EQ_INT(0, res);
  EXPECT_EQ_INT(7000, file_duration.count());

  res = wp_config_callback("basedir", "test/");
  EXPECT_EQ_INT(0, res);
  EXPECT_EQ_STR("test/", base_directory.c_str());

  res = wp_config_callback("buffersize", "1000");
  EXPECT_EQ_INT(0, res);
  EXPECT_EQ_INT(1000, buffer_capacity);

  res = wp_config_callback("bufferduration", "3600");
  EXPECT_EQ_INT(0, res);
  EXPECT_EQ_INT(3600, buffer_duration.count());

  res = wp_config_callback("compression", "zstd");
  EXPECT_EQ_INT(0, res);
  auto props = properties_builder.build();
  EXPECT_EQ_INT(
      parquet::Compression::ZSTD,
      props->compression(parquet::schema::ColumnPath::FromDotString("value")));

  res = wp_config_callback("compression", "off");
  EXPECT_EQ_INT(0, res);
  props = properties_builder.build();
  EXPECT_EQ_INT(
      parquet::Compression::UNCOMPRESSED,
      props->compression(parquet::schema::ColumnPath::FromDotString("value")));

  res = wp_config_callback("compression", "BROTLI");
  EXPECT_EQ_INT(0, res);
  props = properties_builder.build();
  EXPECT_EQ_INT(
      parquet::Compression::BROTLI,
      props->compression(parquet::schema::ColumnPath::FromDotString("value")));

  res = wp_config_callback("compression", "gzip");
  EXPECT_EQ_INT(0, res);
  props = properties_builder.build();
  EXPECT_EQ_INT(
      parquet::Compression::GZIP,
      props->compression(parquet::schema::ColumnPath::FromDotString("value")));

  return 0;
}

DEF_TEST(file_recreation) {
  file_duration = std::chrono::seconds(0);

  File file("./(/non/existent/way/42/)/");

  file.recreate();
  EXPECT_EQ_INT(0, file.is_active());

  file_duration = std::chrono::seconds(5);

  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_EQ_INT(1, file.is_active());
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_EQ_INT(1, file.is_active());
  std::this_thread::sleep_for(std::chrono::seconds(5));
  EXPECT_EQ_INT(0, file.is_active());

  return 0;
}

DEF_TEST(write_all_metrics_to_buffer) {
  buffer_capacity = 1000;
  buffer_duration = std::chrono::seconds(1000);
  file_duration = std::chrono::seconds(1000);

  Writer<int64_t> writer("./(/non/existent/way/42/)/", schema_int);

  for (int64_t i = 0; i < 1000; i++) {
    writer.write(i);
  }
  EXPECT_EQ_INT(1000, buffer_size.load());

  writer.close();
  EXPECT_EQ_INT(0, buffer_size.load());

  return check_result(1000, 'i', get_buffer('i', writer));
}

DEF_TEST(write_with_correct_comression_type) {
  file_duration = std::chrono::seconds(1000);
  properties_builder.compression(parquet::Compression::BROTLI);

  Writer<int64_t> writer("./(/non/existent/way/42/)/", schema_int);

  writer.write(1);
  writer.close();

  auto file_reader = parquet::ParquetFileReader::Open(
      std::make_shared<arrow::io::BufferReader>(get_buffer('i', writer)));

  EXPECT_EQ_INT(
      parquet::Compression::BROTLI,
      file_reader->metadata()->RowGroup(0)->ColumnChunk(0)->compression());

  return 0;
}

DEF_TEST(write_without_buffer) {
  file_duration = std::chrono::seconds(1000);
  buffer_duration = std::chrono::seconds(100);
  buffer_capacity = 0;

  Writer<int64_t> writer("./(/non/existent/way/42/)/", schema_int);

  for (int i = 0; i < 10000; i++) {
    writer.write(i);
    EXPECT_EQ_INT(0, buffer_size);
  }
  writer.close();

  return check_result(1000, 'i', get_buffer('i', writer));
}

DEF_TEST(recreate_writer) {
  file_duration = std::chrono::seconds(1000);
  buffer_duration = std::chrono::seconds(510);
  buffer_capacity = 7000;

  Writer<int64_t> writer("./(/non/existent/way/42/)/", schema_int);
  auto old_buffer = std::static_pointer_cast<arrow::io::BufferOutputStream>(
      writer.file.stream());

  for (int i = 0; i < 10000; i++) {
    writer.write(i);
  }
  EXPECT_EQ_INT(3000, buffer_size);

  writer.open();
  EXPECT_EQ_INT(0, buffer_size);

  if (int err = check_result(10000, 'i', old_buffer->Finish().ValueOrDie())) {
    return err;
  }

  for (int i = 0; i < 2000; i++) {
    writer.write(i);
  }
  EXPECT_EQ_INT(2000, buffer_size);

  writer.close();

  return check_result(2000, 'i', get_buffer('i', writer));
}

DEF_TEST(many_writers) {
  buffer_capacity = 431;

  handler.get_all().clear();
  handler.get<int64_t>("i1", schema_int);
  handler.get<double>("d2", schema_double);
  handler.get<double>("d3", schema_double);
  handler.get<int64_t>("i4", schema_int);

  int modulo = buffer_capacity;
  for (auto &[name, writer] : handler.get_all()) {
    for (int i = 0; i < 1000; i++) {
      if (name[0] == 'i') {
        writer->write(i);
      } else {
        writer->write(i + 0.0);
      }
      EXPECT_EQ_INT((buffer_capacity - modulo) +
                        (modulo == 0 ? 0 : 1 + i % modulo),
                    buffer_size);
    }
    modulo -= modulo == 0 ? 0 : 1 + (1000 - 1) % modulo;
  }

  for (auto &[name, writer] : handler.get_all()) {
    writer->close();
    if (int err = check_result(1000, name[0], get_buffer(name[0], *writer))) {
      return err;
    }
  }
  return 0;
}

DEF_TEST(flush_callback) {
  buffer_capacity = 300;

  handler.get_all().clear();
  handler.get<int64_t>("i1", schema_int);
  handler.get<double>("d2", schema_double);
  handler.get<double>("d3", schema_double);
  handler.get<int64_t>("i4", schema_int);

  int modulo = buffer_capacity;

  std::shared_ptr<arrow::io::OutputStream> old_files[4];
  char types[4];
  int index = 0;

  for (auto &[name, writer] : handler.get_all()) {
    for (int i = 0; i < 1000; i++) {
      if (name[0] == 'i') {
        writer->write(i);
      } else {
        writer->write(i + 0.0);
      }
      EXPECT_EQ_INT((buffer_capacity - modulo) +
                        (modulo == 0 ? 0 : 1 + i % modulo),
                    buffer_size);
    }
    modulo -= modulo == 0 ? 0 : 1 + (1000 - 1) % modulo;
    types[index] = name[0];
    if (name[0] == 'i') {
      old_files[index] = dynamic_cast<Writer<int64_t> &>(*writer).file.stream();
    } else {
      old_files[index] = dynamic_cast<Writer<double> &>(*writer).file.stream();
    }
    index++;
  }

  wp_flush_callback(0, nullptr, nullptr);

  for (int i = 0; i < 4; i++) {
    auto buffer =
        std::static_pointer_cast<arrow::io::BufferOutputStream>(old_files[i])
            ->Finish()
            .ValueOrDie();
    if (int err = check_result(1000, types[i], buffer)) {
      return err;
    }
  }

  for (auto &[name, writer] : handler.get_all()) {
    if (name[0] == 'i') {
      EXPECT_EQ_INT(
          0, dynamic_cast<Writer<int64_t> &>(*writer).file.stream()->closed());
    } else {
      EXPECT_EQ_INT(
          0, dynamic_cast<Writer<double> &>(*writer).file.stream()->closed());
    }
  }

  return 0;
}

DEF_TEST(shutdown_callback) {
  buffer_capacity = 512;

  handler.get_all().clear();
  handler.get<int64_t>("i1", schema_int);
  handler.get<double>("d2", schema_double);
  handler.get<double>("d3", schema_double);
  handler.get<int64_t>("i4", schema_int);

  int modulo = buffer_capacity;

  for (auto &[name, writer] : handler.get_all()) {
    for (int i = 0; i < 1000; i++) {
      if (name[0] == 'i') {
        writer->write(i);
      } else {
        writer->write(i + 0.0);
      }
      EXPECT_EQ_INT((buffer_capacity - modulo) +
                        (modulo == 0 ? 0 : 1 + i % modulo),
                    buffer_size);
    }
    modulo -= modulo == 0 ? 0 : 1 + (1000 - 1) % modulo;
  }

  wp_shutdown_callback();

  for (auto &[name, writer] : handler.get_all()) {
    if (name[0] == 'i') {
      EXPECT_EQ_INT(
          1, dynamic_cast<Writer<int64_t> &>(*writer).file.stream()->closed());
    } else {
      EXPECT_EQ_INT(
          1, dynamic_cast<Writer<double> &>(*writer).file.stream()->closed());
    }
    if (int err = check_result(1000, name[0], get_buffer(name[0], *writer))) {
      return err;
    }
  }
  return 0;
}

DEF_TEST(write_callback_correct) {
  metric_family_t *family =
      (metric_family_t *)calloc(1, sizeof(metric_family_t));
  if (family == nullptr) {
    return 0;
  };
  family->name = sstrdup("test_family");
  family->type = METRIC_TYPE_GAUGE;
  family->resource = {
      .ptr = new label_pair_t[]{sstrdup("host.name"), sstrdup("TestHost")},
      .num = 1};
  family->help = sstrdup("help");

  metric_family_append(family, "option1", "value1", value_t{.gauge = 0}, NULL);
  metric_label_set(family->metric.ptr, "option2", "VALUE_d1");

  base_directory = "/TestHome/";
  handler.get_all().clear();

  EXPECT_EQ_INT(0, wp_write_callback(family, nullptr));

  metric_family_metric_reset(family);
  family->type = METRIC_TYPE_COUNTER;
  metric_family_append(family, "option1", "value_i2", value_t{.counter = 0},
                       NULL);
  metric_family_append(family, "option1", "value_i2", value_t{.counter = 1},
                       NULL);

  EXPECT_EQ_INT(0, wp_write_callback(family, nullptr));
  metric_family_free(family);

  std::vector<std::string> expected_paths{
      "TestHost/test_family/value1/VALUE_d1", "TestHost/test_family/value_i2"};

  for (auto &[path, writer] : handler.get_all()) {
    auto path_iter =
        std::find(expected_paths.begin(), expected_paths.end(), path);
    if (path_iter == expected_paths.end()) {
      return -1;
    }
    expected_paths.erase(path_iter);
    writer->close();
    size_t size = path.size();
    if (int err = check_result(path[size - 1] - '0', path[size - 2],
                               get_buffer(path[size - 2], *writer))) {
      return err;
    }
  }

  return 0;
}

DEF_TEST(write_callback_unsopported) {
  metric_family_t *family =
      (metric_family_t *)calloc(1, sizeof(metric_family_t));
  if (family == nullptr) {
    return 0;
  };
  family->name = sstrdup("test_family");
  family->type = METRIC_TYPE_GAUGE;
  family->resource = {
      .ptr = new label_pair_t[]{sstrdup("host.id"), sstrdup("123456789")},
      .num = 1};
  family->help = sstrdup("help");

  metric_family_append(family, "option", "value", value_t{.gauge = 0}, NULL);
  metric_label_set(family->metric.ptr, "option2", "VALUE_d1");

  EXPECT_EQ_INT(ENOENT, wp_write_callback(family, nullptr));

  metric_family_free(family);
  return 0;
}

int main() {
  RUN_TEST(time_formating);
  RUN_TEST(parse_metric);
  RUN_TEST(config_invalid);
  RUN_TEST(config_correct);
  RUN_TEST(file_recreation);
  RUN_TEST(write_all_metrics_to_buffer);
  RUN_TEST(write_with_correct_comression_type);
  RUN_TEST(write_without_buffer);
  RUN_TEST(recreate_writer);
  RUN_TEST(many_writers);
  RUN_TEST(flush_callback);
  RUN_TEST(shutdown_callback);
  RUN_TEST(write_callback_correct);
  RUN_TEST(write_callback_unsopported);

  END_TEST;
}