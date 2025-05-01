#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <fstream>
#include <filesystem>

#include <grpcpp/server.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server_context.h>
#include <grpcpp/client_context.h>
#include <google/protobuf/empty.pb.h>

#include "thread_safe_queue.h"
#include "message.h"
#include "file_logger.h"
#include "mini3.grpc.pb.h"

class Server {
public:
    Server(std::string node_id,
           std::string listen_addr,
           std::vector<std::string> neighbors);

    // Launch all threads
    void run();
    void shutdown();

    // **New** synchronous task handler
    grpc::Status handleTaskRequest(const mini3::TaskRequest& req,
                                   mini3::TaskResponse* resp);

private:
    // Heartbeat machinery unchanged
    void listenerLoop();
    void heartbeatLoop();
    void routingLoop();
    void metricsLoop();

    // Peer‐info
    struct PeerEntry {
        double score;
        std::string via;
        int hops;
        std::chrono::steady_clock::time_point last_seen;
    };
    void updatePeerInfo(const mini3::Heartbeat& hb,
                        const std::string& sender);

    // Score computation
    static constexpr size_t MAX_QUEUE = 50;
    double computeScore();

    // “Local” execution stub (used by handleTaskRequest)
    void processLocally(const std::string& task_id,
                        mini3::TaskResponse* resp);

    // Forward synchronously
    grpc::Status forwardTaskSync(const mini3::TaskRequest& req,
                                 mini3::TaskResponse* resp);

    // Config & identity
    std::string node_id_;
    std::string listen_addr_;
    std::vector<std::string> neighbors_;

    // Logging & metrics
    std::unique_ptr<FileLogger>  logger_;
    std::ofstream                metrics_file_;
    std::mutex                   metrics_mtx_;

    // gRPC server
    std::unique_ptr<grpc::Server> grpc_server_;

    // Stubs to neighbors
    std::vector<std::unique_ptr<mini3::Mini3Service::Stub>> stubs_;
    std::vector<std::string>                                stub_addrs_;

    // Peer state & routing
    std::unordered_map<std::string, PeerEntry> peer_info_;
    std::unordered_map<std::string, std::string> next_hop_;

    // Threads
    std::atomic<bool>            running_{false};
    std::thread                  t_listener_;
    std::thread                  t_heartbeat_;
    std::thread                  t_routing_;
    std::thread                  t_metrics_;
};

#endif // SERVER_H
