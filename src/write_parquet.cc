
extern "C" {
#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include "utils/strbuf/strbuf.h"
}

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <parquet/arrow/writer.h>
#include <parquet/stream_writer.h>
#include <utility>

#define RETURN_ON_ERROR(e, msg, ...)                                           \
  do {                                                                         \
    int code = static_cast<int>((e));                                          \
    if (code != 0) {                                                           \
      P_ERROR((msg), __VA_ARGS__, code);                                       \
      return code;                                                             \
    }                                                                          \
  } while (0)

using std::chrono::system_clock;
using time_point = system_clock::time_point;
using node_shared_ptr = std::shared_ptr<parquet::schema::GroupNode>;

static const char *config_keys[] = {"basedir", "fileduration", "compression",
                                    "buffersize", "bufferduration"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static std::filesystem::path base_directory = "";

static const inline std::string filename = "active.parquet";
static std::chrono::seconds file_duration = std::chrono::seconds(3600);

static std::chrono::seconds buffer_duration = std::chrono::seconds(300);
static uint64_t buffer_capacity = 10000;
static std::atomic<uint64_t> buffer_size = 0;

static parquet::WriterProperties::Builder properties_builder{};

static node_shared_ptr schema_int =
    std::static_pointer_cast<parquet::schema::GroupNode>(
        parquet::schema::GroupNode::Make(
            "value", parquet::Repetition::OPTIONAL,
            {parquet::schema::PrimitiveNode::Make(
                "value", parquet::Repetition::OPTIONAL, parquet::Type::INT64,
                parquet::ConvertedType::INT_64)}));

static node_shared_ptr schema_double =
    std::static_pointer_cast<parquet::schema::GroupNode>(
        parquet::schema::GroupNode::Make("value", parquet::Repetition::OPTIONAL,
                                         {parquet::schema::Double("value")}));

/**
 * Convert time_point to std::string with given strftime format, output string
 * must be shorter than 24 symbols.
 *
 * @param point time point to be converted
 * @param format strftime conversion format
 * @return time point as std::string
 */
static std::string wf_time_point_to_string(time_point point,
                                           const std::string &format) {
  tm time_tm = {0};
  char time_buf[24] =
      {}; // %Y%m%dT%H%M%S.parquet -> 20241231T150109.parquet -> 23 symbols

  time_t now = system_clock::to_time_t(point);
  localtime_r(&now, &time_tm);
  strftime(time_buf, sizeof(time_buf), format.c_str(), &time_tm);

  return time_buf;
}

namespace { // Anonymous namespace
enum class MetricValueType { DOUBLE, INT64 };

/**
 * Class to create, contain and rename files with settled period(global variable
 * file_duration).
 */
class File {
private:
  std::filesystem::path path{};
  std::string path_str{};

  time_point creation_time{};
  std::shared_ptr<arrow::io::FileOutputStream> file{};

public:
  File(const std::filesystem::path &path)
      : path(path), path_str((path / filename).c_str()) {
    recreate();
  };

  /**
   * Check if the file exists longer than it should be supported.
   *
   * @return true if the file was created no longer than file_duration
   */
  bool is_active() {
    return system_clock::now() - creation_time < file_duration;
  }

  /**
   * Change name from filename to it`s creation timestamp.
   *
   * @return 0 on success, else error code
   */
  int rename() {
    if (not file or file->closed()) {
      return 0;
    }
    RETURN_ON_ERROR(file->Close().code(), "file closing (%s) failed: %i",
                    path_str.c_str());
    std::string time_str =
        wf_time_point_to_string(creation_time, "%Y%m%dT%H%M%S.parquet");

    std::error_code error_code{};
    std::filesystem::rename(path_str, path / time_str, error_code);
    RETURN_ON_ERROR(error_code.value(), "file renaming (%s) failed: %i",
                    path_str.c_str());
    return 0;
  }

  /**
   * Open file with name filename(global variable). If it does not exist,
   * creates such file.
   *
   * @return 0 on success, else error code
   */
  int recreate() {
    auto opening_result = arrow::io::FileOutputStream::Open(path_str, false);
    RETURN_ON_ERROR(opening_result.status().code(),
                    "file opening (%s) failed: %i", path_str.c_str());
    file = std::move(opening_result.ValueOrDie());

    creation_time = system_clock::now();
    return 0;
  }

  /**
   * File writing stream getter
   *
   * @return arrow::io::FileOutputStream
   */
  std::shared_ptr<arrow::io::FileOutputStream> stream() { return file; }
};

/**
 * An abstract class for unifying interfaces for writing double and int64_t.
 */
class IWriter {
public:
  virtual void flush() = 0;

  virtual int close() = 0;

  virtual int open() = 0;

  virtual int write(std::variant<int64_t, double>) = 0;

  virtual ~IWriter() = default;
};

/**
 * Class to write metric in File, provide buffer with constraints on size and
 * lifetime.
 *
 * @tparam DataType Type to be written in parquet file, should be one of double
 * or int64_t
 */
template <typename DataType> class Writer : public IWriter {
private:
  File file;
  parquet::StreamWriter writer;
  node_shared_ptr schema;
  std::mutex file_mutex;

  time_point buffer_flush_time{};
  std::vector<DataType> buffer{};

public:
  Writer(const std::filesystem::path &path, node_shared_ptr schema_)
      : file(path), schema(std::move(schema_)) {
    writer = parquet::StreamWriter{parquet::ParquetFileWriter::Open(
        file.stream(), schema, properties_builder.build())};
  };

  /**
   * Check if the buffer is being filled longer than it should be.
   *
   * @return true if the first row is added no longer than buffer_duration
   */
  bool is_buffer_active() {
    return system_clock::now() - buffer_flush_time < buffer_duration;
  }

  /**
   * Flush all values from buffer to file.
   *
   * Remember what parquet is a binary data format with strong constraints on
   * header and footer. So, to check values in the active file after flush
   * you must correctly close the file with calling close()
   */
  void flush() override {
    for (DataType value : buffer) {
      writer << value << parquet::EndRow;
    }
    buffer_size.fetch_sub(buffer.size());
    buffer_flush_time = system_clock::now();
    buffer.clear();
  }

  /**
   * Flush all values from buffer to file, change it name to timestamp and close
   * ParquetFileWriter.
   *
   * @return 0 on success, else error code
   */
  int close() override {
    flush();
    writer = parquet::StreamWriter{};
    return file.rename();
  }

  /**
   * Close the file, flushing buffered data.
   * Open (or create) a new file named filename and configure the
   * ParquetFileWriter.
   *
   * @return 0 on success, else error code
   */
  int open() override {
    close();
    int err = file.recreate();
    writer = parquet::StreamWriter{parquet::ParquetFileWriter::Open(
        file.stream(), schema, properties_builder.build())};
    return err;
  }

  /**
   * Add data to buffer. If buffer is fulfilled(or has capacity = 0) writes to
   * file. Uses mutex to avoid race condition while writing/creating/renaming
   * file.
   *
   * @param raw_data Value to be saved
   * @return 0 on success, else error code
   */
  int write(std::variant<int64_t, double> raw_data) override {
    std::lock_guard lock(file_mutex);
    DataType data = std::get<DataType>(raw_data);
    if (not file.is_active()) {
      if (int err = open()) {
        return err;
      }
    }
    if (not is_buffer_active()) {
      flush();
    }

    uint64_t new_size = buffer_size.fetch_add(1);
    if (new_size >= buffer_capacity) {
      flush();
      buffer_size.fetch_add(-1);
      writer << data << parquet::EndRow;
    } else {
      buffer.push_back(data);
    }
    return 0;
  }
};

/**
 * Class containing a search tree for determining the directories by names of
 * metrics. Build an hierarchic dirs structure by full metrics names.
 */
class DirectoriesHandler {
  std::map<std::string, std::shared_ptr<IWriter>> dirs{};

public:
  DirectoriesHandler() = default;

  /**
   * Return Writer of described metric.
   * Expand metric name by adding prefix base_directory
   *
   * @tparam DataType data type, contained in metric
   * @param name full name of metric (such a /host/family/all_labels.../)
   * @param schema parquet file data schema, must match the DataType
   * @return shared pointer on Writer corresponding to the input metric
   */
  template <typename DataType>
  std::shared_ptr<IWriter> get(const std::string &name,
                               const node_shared_ptr &schema) {
    if (dirs.find(name) != dirs.end()) {
      return dirs.at(name);
    }
    std::error_code error_code{};
    std::filesystem::create_directories(base_directory / name, error_code);
    if (error_code) {
      P_ERROR("directory creating (%s) error: %s",
              (base_directory / name).c_str(), error_code.message().c_str());
      return nullptr;
    }
    std::shared_ptr<IWriter> writer =
        std::make_shared<Writer<DataType>>(base_directory / name, schema);
    dirs.emplace(name, writer);
    return writer;
  }

  /**
   * Getter on dirs
   *
   * @return reference to std::map. containing all created Writers
   */
  std::map<std::string, std::shared_ptr<IWriter>> &get_all() { return dirs; }
};
} // Anonymous namespace

static DirectoriesHandler handler{};

/**
 * Return value of metric with MetricValueType DOUBLE
 *
 * @param mt metric, which value will be returned
 * @return value
 */
static double wf_parse_metric_double(const metric_t *mt) {
  switch (mt->family->type) {
  case METRIC_TYPE_GAUGE:
    return mt->value.gauge;
  case METRIC_TYPE_COUNTER_FP:
    return mt->value.counter_fp;
  case METRIC_TYPE_UP_DOWN_FP:
    return mt->value.up_down_fp;
  default:
    break;
  }
  return 0;
}

/**
 * Return value of metric with MetricValueType INT64
 *
 * @param mt metric, which value will be returned
 * @return value
 */
static int64_t wf_parse_metric_int(const metric_t *mt) {
  switch (mt->family->type) {
  case METRIC_TYPE_COUNTER:
    return mt->value.counter;
  case METRIC_TYPE_UP_DOWN:
    return mt->value.up_down;
  default:
    break;
  }
  return 0;
}

/**
 * Determines metric`s value type
 *
 * @param mt metric
 * @return type of metric
 */
static MetricValueType wf_get_metric_type(const metric_t *mt) {
  switch (mt->family->type) {
  case METRIC_TYPE_GAUGE:
    return MetricValueType::DOUBLE;
  case METRIC_TYPE_COUNTER:
    return MetricValueType::INT64;
  case METRIC_TYPE_COUNTER_FP:
    return MetricValueType::DOUBLE;
  case METRIC_TYPE_UP_DOWN:
    return MetricValueType::INT64;
  case METRIC_TYPE_UP_DOWN_FP:
    return MetricValueType::DOUBLE;
  default:
    break;
  }
  return MetricValueType::DOUBLE;
}

static int wf_write_callback(metric_family_t const *fam,
                             user_data_t *user_data) {
  auto host = label_set_get(fam->resource, "host.name");
  if (not host) {
    P_ERROR("Expected host.name as metric family resource");
    return ENOENT;
  }
  std::filesystem::path base;
  std::string_view clear_host = host;
  while (!clear_host.empty() and clear_host.back() == '.') {
    clear_host.remove_suffix(1);
  }
  base /= clear_host;
  base /= fam->name;

  for (size_t i = 0; i < fam->metric.num; i++) {
    metric_t *mt = fam->metric.ptr + i;
    std::filesystem::path full_path = base;
    for (size_t j = 0; j < mt->label.num; j++) {
      label_pair_t *lab = mt->label.ptr + j;
      full_path /= lab->value;
    }
    MetricValueType type = wf_get_metric_type(mt);
    if (type == MetricValueType::DOUBLE) {
      auto writer = handler.get<double>(full_path.string(), schema_double);
      writer->write(wf_parse_metric_double(mt));
    } else if (type == MetricValueType::INT64) {
      auto writer = handler.get<int64_t>(full_path.string(), schema_int);
      writer->write(wf_parse_metric_int(mt));
    }
  }
  return 0;
}

static int wf_config_callback(const char *key, const char *value) {
  if (strcasecmp("basedir", key) == 0) {
    base_directory = value;
  } else if (strcasecmp("fileduration", key) == 0) {
    file_duration = std::chrono::seconds(std::strtoul(value, nullptr, 10));
  } else if (strcasecmp("bufferduration", key) == 0) {
    buffer_duration = std::chrono::seconds(std::strtoul(value, nullptr, 10));
  } else if (strcasecmp("buffersize", key) == 0) {
    buffer_capacity = std::strtoul(value, nullptr, 10);
  } else if (strcasecmp("compression", key) == 0) {
    if (strcasecmp("uncompressed", value) == 0 or
        strcasecmp("off", value) == 0) {
      properties_builder.compression(parquet::Compression::UNCOMPRESSED);
    } else if (strcasecmp("brotli", value) == 0) {
      properties_builder.compression(parquet::Compression::BROTLI);
    } else if (strcasecmp("gzip", value) == 0) {
      properties_builder.compression(parquet::Compression::GZIP);
    } else if (strcasecmp("zstd", value) == 0) {
      properties_builder.compression(parquet::Compression::ZSTD);
    } else {
      P_ERROR("Invalid compression type (%s)", value);
      return EINVAL;
    }
  } else {
    P_ERROR("Invalid configuration option (%s)", key);
    return -EINVAL;
  }
  return 0;
}

static int wf_init_callback() {
  if (buffer_duration > file_duration) {
    P_ERROR("Buffer containing duration(%lu) must be less than file existing "
            "time(%lu)",
            buffer_duration.count(), file_duration.count());
    return EINVAL;
  }
  return 0;
}

static int wf_flush_callback(cdtime_t timeout, const char *identifier,
                             user_data_t *user_data) {
  if (timeout > 0) {
    return 0;
  }
  for (auto &[path, writer] : handler.get_all()) {
    writer->flush();
  }
  return 0;
}

static int wf_shutdown_callback() {
  for (auto &[path, writer] : handler.get_all()) {
    if (int err = writer->close()) {
      return err;
    }
  }
  return 0;
}

extern "C" {
void module_register(void) {
  plugin_register_config("write_parquet", wf_config_callback, config_keys,
                         config_keys_num);
  plugin_register_init("write_parquet", wf_init_callback);
  plugin_register_write("write_parquet", wf_write_callback, NULL);
  plugin_register_flush("write_parquet", wf_flush_callback, NULL);
  plugin_register_shutdown("write_parquet", wf_shutdown_callback);
}
}