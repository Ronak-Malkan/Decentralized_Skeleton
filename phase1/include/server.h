// server.h
#ifndef MINI3_SERVER_H
#define MINI3_SERVER_H

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

#include "mini3.grpc.pb.h"
#include "thread_safe_queue.h"
#include "file_logger.h"

#include <map>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <chrono>
#include <filesystem>

constexpr size_t MAX_QUEUE = 1000;

enum class MessageType { HEARTBEAT, TASK_REQUEST };

struct Message {
  MessageType type;
  std::string from;
  mini3::Heartbeat heartbeat;
  mini3::TaskRequest task_request;
};

struct PeerEntry {
  double score;
  std::string via;
  int hops;
  std::chrono::steady_clock::time_point last_seen;
};

class Server final {
public:
  Server(std::string node_id,
         std::string listen_addr,
         std::vector<std::string> neighbors);

  // start all threads and gRPC server
  void run();

  // initiate shutdown
  void shutdown();

private:
  // per‐thread loops
  void listenerLoop();
  void processorLoop();
  void heartbeatLoop();
  void routingLoop();
  void metricsLoop();

  // internal helpers
  void updatePeerInfo(const mini3::Heartbeat& hb, const std::string& sender);
  void processLocal(const Message& msg);
  void forwardTask(const Message& msg);
  double computeScore();

  // identity & network
  std::string node_id_;
  std::string listen_addr_;
  std::vector<std::string> neighbors_;

  // gRPC stubs
  std::vector<std::unique_ptr<mini3::Mini3Service::Stub>> stubs_;
  std::vector<std::string> stub_addrs_;
  std::unique_ptr<grpc::Server> grpc_server_;

  // incoming message queue
  ThreadSafeQueue<Message> inbound_;

  // peer state, protected by peers_mtx_
  std::mutex peers_mtx_;
  std::map<std::string, PeerEntry> peer_info_;
  std::map<std::string, std::string>  next_hop_;

  // threads and control flag
  std::thread t_listener_, t_processor_, t_heartbeat_, t_routing_, t_metrics_;
  std::atomic<bool> running_{false};

  // logging & metrics
  std::unique_ptr<FileLogger> logger_;
  std::ofstream metrics_file_;
  std::mutex metrics_mtx_;
};

#endif // MINI3_SERVER_H
