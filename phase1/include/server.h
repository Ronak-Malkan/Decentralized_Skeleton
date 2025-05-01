#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <thread>
#include <mutex>
#include <fstream>

#include "thread_safe_queue.h"   // your queue, now only used for heartbeats
#include "file_logger.h"         // FileLogger
#include "mini3.grpc.pb.h"       // generated gRPC + protobuf

// Maximum queue length for score computation
static constexpr size_t MAX_QUEUE = 1024;

struct PeerEntry {
  double                              score;
  std::string                         via;
  int                                 hops;
  std::chrono::steady_clock::time_point last_seen;
};

class Server {
public:
  Server(std::string node_id,
         std::string listen_addr,
         std::vector<std::string> neighbors);
  ~Server();

  // Starts all the threads and blocks on the gRPC server
  void run();
  void shutdown();

private:
  // gRPC listener (for both heartbeat & task RPCs)
  void listenerLoop();

  // Only processes heartbeats from the queue
  void processorLoop();

  // Periodically send heartbeats to neighbors
  void heartbeatLoop();

  // Prune stale peers and rebuild next_hop_ map
  void routingLoop();

  // Dump metrics once a second
  void metricsLoop();

  // Score computation from load, queue, uptime...
  double computeScore();

  // Update peer_info_ from an incoming heartbeat
  void updatePeerInfo(const mini3::Heartbeat& hb,
                      const std::string& sender);

  // Called when we decide to handle a task locally (sync path)
  void processLocalSync(const mini3::TaskRequest& req,
                        mini3::TaskResponse* resp);

  // Configuration / state
  const std::string                          node_id_;
  const std::string                          listen_addr_;
  const std::vector<std::string>             neighbors_;

  // one stub per neighbor
  std::vector<std::unique_ptr<mini3::Mini3Service::Stub>> stubs_;
  std::vector<std::string>                    stub_addrs_;

  // Logging + queue for heartbeats only
  std::unique_ptr<FileLogger>                 logger_;
  ThreadSafeQueue<mini3::Heartbeat>           hb_queue_;

  // Peer table + routing
  std::unordered_map<std::string,PeerEntry>   peer_info_;
  std::unordered_map<std::string,std::string> next_hop_;

  // gRPC server handle
  std::shared_ptr<grpc::Server>               grpc_server_;

  // Threads
  std::atomic<bool>                           running_{false};
  std::thread                                 t_listener_,
                                              t_processor_,
                                              t_heartbeat_,
                                              t_routing_,
                                              t_metrics_;

  // Metrics file
  std::mutex                                  metrics_mtx_;
  std::ofstream                               metrics_file_;
};
