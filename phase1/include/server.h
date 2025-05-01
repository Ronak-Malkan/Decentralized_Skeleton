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

#include <grpcpp/grpcpp.h>

#include "thread_safe_queue.h"
#include "message.h"
#include "mini3.grpc.pb.h"
#include "file_logger.h"

class Server {
public:
    Server(std::string node_id,
           std::string listen_addr,
           std::vector<std::string> neighbors);

    // Launch all threads and block
    void run();

    // Signal shutdown
    void shutdown();

private:
    // Main loops
    void listenerLoop();
    void processorLoop();
    void heartbeatLoop();
    void routingLoop();
    void metricsLoop();

    // Task handling
    void processLocal(const Message& msg);
    void forwardTask(const Message& msg);

    // Peer-info update
    struct PeerEntry {
        double score;
        std::string via;
        int hops;
        std::chrono::steady_clock::time_point last_seen;
    };
    void updatePeerInfo(const mini3::Heartbeat& hb, const std::string& sender);

    // Metrics & scoring
    static constexpr size_t MAX_QUEUE = 50;
    double computeScore();

    // Config & identity
    std::string node_id_;
    std::string listen_addr_;
    std::vector<std::string> neighbors_;

    // Incoming message queue
    ThreadSafeQueue<Message> inbound_;
    std::atomic<bool>          running_{false};

    // Threads
    std::thread t_listener_;
    std::thread t_processor_;
    std::thread t_heartbeat_;
    std::thread t_routing_;
    std::thread t_metrics_;

    // gRPC server
    std::unique_ptr<grpc::Server> grpc_server_;

    // gRPC stubs for neighbors
    std::vector<std::unique_ptr<mini3::Mini3Service::Stub>> stubs_;
    std::vector<std::string>                               stub_addrs_;

    // Peer state & routing
    std::unordered_map<std::string,PeerEntry> peer_info_;
    std::unordered_map<std::string,std::string> next_hop_;

    // Logging
    std::unique_ptr<FileLogger> logger_;

    // Metrics output
    std::ofstream metrics_file_;
    std::mutex    metrics_mtx_;
};

#endif // SERVER_H
