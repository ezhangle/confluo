#ifndef PACKETLOADER_H_
#define PACKETLOADER_H_

#include <chrono>
#include <sys/time.h>
#include <random>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <unistd.h>

#include "logstore.h"

#ifdef NO_LOG
#define LOG(out, fmt, ...)
#else
#define LOG(out, fmt, ...) fprintf(out, fmt, ##__VA_ARGS__)
#endif

using namespace ::slog;
using namespace ::std::chrono;

class rate_limiter {
 public:
  rate_limiter(uint64_t pkts_per_sec, log_store::handle* handle) {
    handle_ = handle;
    dur_.tv_sec = 0;
    dur_.tv_nsec = 0;
    sleep_ns_ = 1e9 / pkts_per_sec;
    LOG(stderr, "Send one packet per %lld ns.\n", sleep_ns_);
  }

  uint64_t insert_packet(unsigned char* data, uint16_t len, tokens& tkns) {
    auto t0 = high_resolution_clock::now();
    uint64_t num_pkts = handle_->insert(data, len, tkns) + 1;
    auto t1 = high_resolution_clock::now();
    dur_.tv_nsec = sleep_ns_ - duration_cast<nanoseconds>(t1 - t0).count();
    if (dur_.tv_nsec > 0) {
      nanosleep(&dur_, NULL);
    }
    return num_pkts;
  }

 private:
  struct timespec dur_;
  int64_t sleep_ns_;
  log_store::handle* handle_;
};

class rate_limiter_inf {
 public:
  rate_limiter_inf(uint64_t pkts_per_sec, log_store::handle* handle) {
    handle_ = handle;
  }

  uint64_t insert_packet(unsigned char* data, uint16_t len, tokens& tkns) {
    return handle_->insert(data, len, tkns) + 1;
  }

 private:
  log_store::handle* handle_;
};

template<class rlimiter = rate_limiter_inf>
class packet_loader {
 public:
  typedef unsigned long long int timestamp_t;
  static const uint64_t kReportRecordInterval = 1000000;

  uint64_t insert_packet(log_store::handle* handle, uint64_t idx) {
    tokens tkns;
    init_tokens(tkns, idx);
    return handle->insert(datas_[idx], datalens_[idx], tkns) + 1;
  }

  void init_tokens(tokens& tkns, uint64_t idx) {
    tkns.time = (unsigned char*) (&timestamps_[idx]);
    tkns.src_ip = (unsigned char*) (&srcips_[idx]);
    tkns.dst_ip = (unsigned char*) (&dstips_[idx]);
    tkns.src_prt = (unsigned char*) (&sports_[idx]);
    tkns.dst_prt = (unsigned char*) (&dports_[idx]);
  }

  uint32_t parse_ip(std::string& ip) {
    uint32_t byte0 = 0, byte1 = 0, byte2 = 0, byte3 = 0;
    sscanf(ip.c_str(), "%u.%u.%u.%u", &byte3, &byte2, &byte1, &byte0);
    return byte3 | byte2 << 8 | byte1 << 16 | byte0 << 24;
  }

  uint32_t parse_time(uint32_t time) {
    unsigned char* timearr = (unsigned char*) (&time);
    return timearr[2] | timearr[1] << 8 | timearr[0] << 16;
  }

  uint16_t parse_port(uint16_t port) {
    unsigned char* portarr = (unsigned char*) (&port);
    return portarr[1] | portarr[0] << 8;
  }

  packet_loader(std::string& data_path, std::string& attr_path,
                std::string& hostname) {
    char resolved_path[100];
    realpath(data_path.c_str(), resolved_path);
    data_path_ = std::string(resolved_path);
    realpath(attr_path.c_str(), resolved_path);
    attr_path_ = std::string(resolved_path);
    hostname_ = hostname;

    logstore_ = new log_store();

    LOG(stderr, "Loading data...\n");
    load_data();

    LOG(stderr, "Initialization complete.\n");
  }

  void load_data() {
    std::ifstream ind(data_path_);
    std::ifstream ina(attr_path_);
    std::string attr_line;
    LOG(stderr, "Reading from path data=%s, attr=%s\n", data_path_.c_str(),
        attr_path_.c_str());

    while (std::getline(ina, attr_line)) {
      std::stringstream attr_stream(attr_line);
      uint32_t ts;
      std::string srcip, dstip;
      uint16_t sport, dport;
      uint16_t len;
      attr_stream >> ts >> len >> srcip >> dstip >> sport >> dport;
      unsigned char* data = new unsigned char[len];
      ind.read((char*) data, len);
      timestamps_.push_back(parse_time(ts));
      srcips_.push_back(parse_ip(srcip));
      dstips_.push_back(parse_ip(srcip));
      sports_.push_back(parse_port(sport));
      dports_.push_back(parse_port(dport));
      datas_.push_back(data);
      datalens_.push_back(len);
    }

    LOG(stderr, "Loaded %zu packets.\n", datas_.size());
  }

  // Throughput benchmarks
  void load_packets(const uint32_t num_threads, const uint64_t timebound,
                    const uint64_t rate_limit) {

    std::vector<std::thread> threads;
    uint64_t thread_ops = timestamps_.size() / num_threads;
    uint64_t local_rate_limit = rate_limit / num_threads;

    LOG(stderr, "Setting timebound to %llu us\n", timebound);
    for (uint32_t i = 0; i < num_threads; i++) {
      threads.push_back(
          std::move(
              std::thread(
                  [i, timebound, local_rate_limit, thread_ops, this] {
                    uint64_t idx = thread_ops * i;
                    tokens tkns;
                    log_store::handle* handle = logstore_->get_handle();
                    rlimiter* limiter = new rlimiter(local_rate_limit, handle);
                    double throughput = 0;
                    std::ofstream rfs;
                    rfs.open("record_progress_" + std::to_string(i));
                    LOG(stderr, "Starting benchmark.\n");

                    try {
                      int64_t local_ops = 0;
                      int64_t total_ops = 0;

                      timestamp_t start = get_timestamp();
                      while (local_ops < thread_ops && get_timestamp() - start < timebound) {
                        init_tokens(tkns, idx);
                        total_ops = limiter->insert_packet(datas_[idx], datalens_[idx], tkns);
                        idx++;
                        local_ops++;
                        if (total_ops % kReportRecordInterval == 0) {
                          rfs << get_timestamp() << "\t" << total_ops << "\n";
                        }
                      }
                      timestamp_t end = get_timestamp();
                      double totsecs = (double) (end - start) / (1000.0 * 1000.0);
                      throughput = ((double) local_ops / totsecs);
                      LOG(stderr, "Thread #%u finished in %lf s. Throughput: %lf.\n", i, totsecs, throughput);
                    } catch (std::exception &e) {
                      LOG(stderr, "Throughput thread ended prematurely.\n");
                    }

                    std::ofstream ofs("write_throughput_" + std::to_string(i));
                    ofs << throughput << "\n";
                    ofs.close();
                    delete limiter;
                    delete handle;
                  })));
    }

    for (auto& th : threads) {
      th.join();
    }

  }

 private:
  static timestamp_t get_timestamp() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
  }

  std::string data_path_;
  std::string attr_path_;
  std::string hostname_;

  std::vector<uint32_t> timestamps_;
  std::vector<uint32_t> srcips_;
  std::vector<uint32_t> dstips_;
  std::vector<uint16_t> sports_;
  std::vector<uint16_t> dports_;
  std::vector<unsigned char*> datas_;
  std::vector<uint16_t> datalens_;

  log_store *logstore_;
};

#endif /* RAMCLOUDBENCHMARK_H_ */
