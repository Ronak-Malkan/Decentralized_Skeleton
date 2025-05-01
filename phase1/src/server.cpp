// src/server.cpp

#include "server.h"

#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <google/protobuf/empty.pb.h>
#include "mini3.grpc.pb.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <sys/sysctl.h>
#include <filesystem>

using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::Status;
using mini3::Heartbeat;
using mini3::Mini3Service;
using mini3::TaskRequest;
using mini3::TaskResponse;
using google::protobuf::Empty;

//----------------------------------------------------------------------------
Server::Server(std::string node_id,
               std::string listen_addr,
               std::vector<std::string> neighbors)
  : node_id_(std::move(node_id)),
    listen_addr_(std::move(listen_addr)),
    neighbors_(std::move(neighbors))
{
    logger_ = std::make_unique<FileLogger>(node_id_);
    logger_->log("server_started", {{"listen_addr", listen_addr_}});

    std::filesystem::create_directories("logs");
    metrics_file_.open("logs/" + node_id_ + ".metrics.jsonl",
                       std::ios::out | std::ios::app);
    if (!metrics_file_) {
        logger_->log("metrics_file_error", {{"path", node_id_ + ".metrics.jsonl"}});
    } else {
        logger_->log("metrics_started");
    }

    // Build one stub per neighbor (skip self)
    for (auto& addr : neighbors_) {
        if (addr == listen_addr_) {
            logger_->log("skip_neighbor_self", {{"addr", addr}});
            continue;
        }
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        stubs_.push_back(Mini3Service::NewStub(channel));
        stub_addrs_.push_back(addr);
    }
    logger_->log("neighbors_count", {{"neighbors", neighbors_.size()}});
    logger_->log("stubs_count",     {{"stubs",     stub_addrs_.size()}});
}

//----------------------------------------------------------------------------
double Server::computeScore() {
    double load = 0.0;
    if (getloadavg(&load,1) != -1) {
        unsigned cores = std::thread::hardware_concurrency();
        double usage = cores ? (load/cores) : load;
        usage = std::clamp(usage, 0.0, (double)cores);
        load = (1.0 - usage/cores) * 100.0;
    } else {
        load = 50.0;
    }

    double mem_free_pct = 50.0;
    struct timeval boottime; size_t len = sizeof(boottime);
    if (sysctlbyname("kern.boottime",&boottime,&len,nullptr,0) != 0) {
        boottime.tv_sec = 0;
    }
    auto now_sys = std::chrono::system_clock::now();
    auto boot = std::chrono::system_clock::from_time_t(boottime.tv_sec);
    double uptime = std::chrono::duration<double>(now_sys - boot).count();

    double q_len = static_cast<double>(inbound_.size());
    double x1 = 1.0 - std::min(q_len, (double)MAX_QUEUE) / MAX_QUEUE;

    return 0.5*(load/100.0)
         + 0.2*x1
         + 0.2*(mem_free_pct/100.0)
         + 0.1*(std::min(uptime,86400.0)/86400.0);
}

//----------------------------------------------------------------------------
void Server::run() {
    running_ = true;
    logger_->log("threads_starting");

    t_listener_  = std::thread(&Server::listenerLoop,   this);
    t_processor_ = std::thread(&Server::processorLoop,  this);
    t_heartbeat_ = std::thread(&Server::heartbeatLoop,  this);
    t_routing_   = std::thread(&Server::routingLoop,    this);
    t_metrics_   = std::thread(&Server::metricsLoop,    this);

    t_listener_.join();
    t_processor_.join();
    t_heartbeat_.join();
    t_routing_.join();
    t_metrics_.join();
}

//----------------------------------------------------------------------------
void Server::shutdown() {
    running_ = false;
    if (grpc_server_) grpc_server_->Shutdown();
    logger_->log("server_shutdown");
}

//----------------------------------------------------------------------------
void Server::listenerLoop() {
    logger_->log("thread_started", {{"thread","listener"}});

    // Our RPC service, with synchronous SubmitTask
    class ServiceImpl final : public Mini3Service::Service {
        ThreadSafeQueue<Message>& queue_;
        FileLogger&              logger_;
        const std::string&       node_id_;
    public:
        ServiceImpl(ThreadSafeQueue<Message>& q,
                    FileLogger& log,
                    const std::string& nid)
          : queue_(q), logger_(log), node_id_(nid) {}

        Status SendHeartbeat(ServerContext*, const Heartbeat* req, Empty*) override {
            logger_.log("heartbeat_received", {
                {"from", req->from()},
                {"gossip_size", req->gossip_size()}
            });
            Message m{MessageType::HEARTBEAT, req->from(), *req, {}};
            queue_.push(std::move(m));
            return Status::OK;
        }

        // Handle TaskRequest synchronously and reply immediately
        Status SubmitTask(ServerContext*,
                          const TaskRequest* req,
                          TaskResponse* resp) override {
            logger_.log("task_start", {{"task", req->task_id()}});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            resp->set_task_id(req->task_id());
            resp->set_result("Processed by " + node_id_);
            logger_.log("task_complete", {{"task", req->task_id()}});
            return Status::OK;
        }
    };

    // Instantiate service with our node_id_
    ServiceImpl service{inbound_, *logger_, node_id_};

    ServerBuilder builder;
    builder.AddListeningPort(listen_addr_, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    grpc_server_ = builder.BuildAndStart();

    logger_->log("grpc_listening", {{"addr", listen_addr_}});
    grpc_server_->Wait();
}

//----------------------------------------------------------------------------
void Server::processorLoop() {
    logger_->log("thread_started", {{"thread","processor"}});
    while (running_) {
        Message msg = inbound_.wait_and_pop();
        if (msg.type == MessageType::HEARTBEAT) {
            updatePeerInfo(msg.heartbeat, msg.from);
        } else {
            double myScore = computeScore();
            double bestScore = myScore;
            for (auto& [id,e] : peer_info_)
                bestScore = std::max(bestScore, e.score);

            if (myScore >= bestScore) processLocal(msg);
            else                     forwardTask(msg);
        }
    }
    logger_->log("thread_exiting", {{"thread","processor"}});
}

//----------------------------------------------------------------------------
void Server::updatePeerInfo(const Heartbeat& hb,
                            const std::string& sender) {
    auto now = std::chrono::steady_clock::now();
    peer_info_[sender] = PeerEntry{hb.my_score(), sender, 0, now};
    for (auto& pi : hb.gossip()) {
        int nh = pi.hops() + 1;
        auto it = peer_info_.find(pi.node_id());
        bool upd = it == peer_info_.end()
                || nh < it->second.hops
                || (nh == it->second.hops && pi.score() > it->second.score);
        if (upd) {
            peer_info_[pi.node_id()] = PeerEntry{pi.score(), sender, nh, now};
        }
    }
    logger_->log("peer_update", {
        {"sender", sender},
        {"known_peers", peer_info_.size()}
    });
}

//----------------------------------------------------------------------------
void Server::processLocal(const Message& msg) {
    // (unused under synchronous SubmitTask)
}

//----------------------------------------------------------------------------
void Server::forwardTask(const Message& msg) {
    // (unused under synchronous SubmitTask)
}

//----------------------------------------------------------------------------
void Server::heartbeatLoop() {
    logger_->log("thread_started", {{"thread","heartbeat"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    while (running_) {
        double score = computeScore();
        Heartbeat hb;
        hb.set_from(node_id_);
        hb.set_my_score(score);
        logger_->log("heartbeat_sent", {
            {"score", score},
            {"gossip_size", hb.gossip_size()}
        });

        for (size_t i = 0; i < stubs_.size(); ++i) {
            auto addr = stub_addrs_[i];
            std::thread([this, i, addr, hb]() {
                ClientContext ctx;
                ctx.set_wait_for_ready(true);
                Empty rep;
                Status s = stubs_[i]->SendHeartbeat(&ctx, hb, &rep);
                if (s.ok()) {
                    logger_->log("heartbeat_ok", {{"to", addr}});
                } else {
                    logger_->log("heartbeat_fail", {
                        {"to",    addr},
                        {"error", s.error_message()}
                    });
                }
            }).detach();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    logger_->log("thread_exiting", {{"thread","heartbeat"}});
}

//----------------------------------------------------------------------------
void Server::routingLoop() {
    logger_->log("thread_started", {{"thread","routing"}});
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        for (auto it = peer_info_.begin(); it != peer_info_.end();) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.last_seen).count();
            if (age > 1500) {
                logger_->log("peer_pruned", {{"peer", it->first}});
                it = peer_info_.erase(it);
            } else ++it;
        }
        next_hop_.clear();
        for (auto& [id,e] : peer_info_) {
            next_hop_[id] = e.via;
        }
        logger_->log("routing_rebuilt", {
            {"entries", next_hop_.size()}
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    logger_->log("thread_exiting", {{"thread","routing"}});
}

//----------------------------------------------------------------------------
void Server::metricsLoop() {
    logger_->log("thread_started", {{"thread","metrics"}});
    while (running_) {
        double load = 0.0;
        if (getloadavg(&load,1) != -1) {
            unsigned cores = std::thread::hardware_concurrency();
            double usage = cores ? (load/cores) : load;
            usage = std::clamp(usage, 0.0, (double)cores);
            load = (1.0 - usage/cores)*100.0;
        }
        double mem_free_pct = 50.0;
        struct timeval boottime; size_t len = sizeof(boottime);
        double uptime = 0.0;
        if (sysctlbyname("kern.boottime",&boottime,&len,nullptr,0)==0) {
            auto now = std::chrono::system_clock::now();
            auto boot = std::chrono::system_clock::from_time_t(boottime.tv_sec);
            uptime = std::chrono::duration<double>(now - boot).count();
        }
        size_t q_len = inbound_.size();
        double score = computeScore();

        nlohmann::json m;
        m["ts"]           = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch()
                            ).count();
        m["cpu_free_pct"] = load;
        m["mem_free_pct"] = mem_free_pct;
        m["queue_length"] = q_len;
        m["uptime_sec"]   = uptime;
        m["score"]        = score;

        {
            std::lock_guard<std::mutex> lk(metrics_mtx_);
            metrics_file_ << m.dump() << "\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    logger_->log("thread_exiting", {{"thread","metrics"}});
}
