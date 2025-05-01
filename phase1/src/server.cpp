#include "server.h"

#include <grpcpp/server_builder.h>
#include <google/protobuf/empty.pb.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <sys/sysctl.h>
#include <filesystem>

using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;
using google::protobuf::Empty;
using mini3::Heartbeat;
using mini3::TaskRequest;
using mini3::TaskResponse;
using mini3::Mini3Service;

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

    for (auto& addr : neighbors_) {
        if (addr == listen_addr_) {
            logger_->log("skip_neighbor_self", {{"addr", addr}});
            continue;
        }
        auto chan = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        stubs_.push_back(Mini3Service::NewStub(chan));
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
    struct timeval bt; size_t len = sizeof(bt);
    if (sysctlbyname("kern.boottime",&bt,&len,nullptr,0)!=0) bt.tv_sec=0;
    auto now  = std::chrono::system_clock::now();
    auto boot = std::chrono::system_clock::from_time_t(bt.tv_sec);
    double uptime = std::chrono::duration<double>(now - boot).count();

    // we no longer queue TaskRequests, so just use zero
    double q_len = 0.0;
    double x1    = 1.0 - std::min(q_len, (double)MAX_QUEUE)/MAX_QUEUE;

    return 0.5*(load/100.0)
         + 0.2*x1
         + 0.2*(mem_free_pct/100.0)
         + 0.1*(std::min(uptime,86400.0)/86400.0);
}

//----------------------------------------------------------------------------
void Server::run() {
    running_ = true;
    t_listener_  = std::thread(&Server::listenerLoop, this);
    t_heartbeat_ = std::thread(&Server::heartbeatLoop, this);
    t_routing_   = std::thread(&Server::routingLoop, this);
    t_metrics_   = std::thread(&Server::metricsLoop, this);

    t_listener_.join();
    t_heartbeat_.join();
    t_routing_.join();
    t_metrics_.join();
}

void Server::shutdown() {
    running_ = false;
    if (grpc_server_) grpc_server_->Shutdown();
    logger_->log("server_shutdown");
}

//----------------------------------------------------------------------------
void Server::listenerLoop() {
    logger_->log("thread_started", {{"thread","listener"}});

    class SvcImpl final : public Mini3Service::Service {
        Server& srv_;
    public:
        explicit SvcImpl(Server& s) : srv_(s) {}
        Status SendHeartbeat(ServerContext*, const Heartbeat* req, Empty*) override {
            srv_.logger_->log("heartbeat_received", {
                {"from",        req->from()},
                {"gossip_size", req->gossip_size()}
            });
            srv_.updatePeerInfo(*req, req->from());
            return Status::OK;
        }
        Status SubmitTask(ServerContext*, const TaskRequest* req, TaskResponse* resp) override {
            return srv_.handleTaskRequest(*req, resp);
        }
    };

    SvcImpl service{*this};
    ServerBuilder builder;
    builder.AddListeningPort(listen_addr_, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    grpc_server_ = builder.BuildAndStart();
    logger_->log("grpc_listening", {{"addr", listen_addr_}});
    grpc_server_->Wait();
}

//----------------------------------------------------------------------------
Status Server::handleTaskRequest(const TaskRequest& req,
                                 TaskResponse* resp) {
    double myScore = computeScore();
    double bestScore = myScore;
    std::string bestId = node_id_;
    for (auto& [id,e] : peer_info_) {
        if (e.score > bestScore) {
            bestScore = e.score;
            bestId = id;
        }
    }

    if (bestId == node_id_) {
        processLocally(req.task_id(), resp);
        return Status::OK;
    } else {
        return forwardTaskSync(req, resp);
    }
}

//----------------------------------------------------------------------------
void Server::processLocally(const std::string& task_id,
                            TaskResponse* resp) {
    logger_->log("task_start", {{"task", task_id}});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    resp->set_task_id(task_id);
    resp->set_result("Processed by " + node_id_);
    logger_->log("task_complete", {{"task", task_id}});
}

//----------------------------------------------------------------------------
Status Server::forwardTaskSync(const TaskRequest& req,
                               TaskResponse* resp) {
    std::string bestId; double bestScore = -1e9;
    for (auto& [id,e] : peer_info_) {
        if (e.score > bestScore) {
            bestScore = e.score;
            bestId = id;
        }
    }
    auto viaIt = next_hop_.find(bestId);
    if (viaIt == next_hop_.end()) {
        logger_->log("task_drop", {{"task", req.task_id()}});
        return Status(StatusCode::UNAVAILABLE, "No route");
    }
    const auto& via = viaIt->second;
    auto it = std::find(stub_addrs_.begin(), stub_addrs_.end(), via);
    size_t idx = std::distance(stub_addrs_.begin(), it);

    ClientContext ctx;
    ctx.set_wait_for_ready(true);
    Status s = stubs_[idx]->SubmitTask(&ctx, req, resp);
    if (s.ok()) {
        logger_->log("task_forward_ok", {{"task", req.task_id()}});
    } else {
        logger_->log("task_forward_fail", {
            {"task",  req.task_id()},
            {"error", s.error_message()}
        });
    }
    return s;
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
        {"sender",      sender},
        {"known_peers", peer_info_.size()}
    });
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
            {"score",       score},
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
            } else {
                ++it;
            }
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
        if (getloadavg(&load,1)!=-1) {
            unsigned cores = std::thread::hardware_concurrency();
            double usage = cores ? (load/cores) : load;
            usage = std::clamp(usage,0.0,(double)cores);
            load = (1.0 - usage/cores)*100.0;
        }
        double mem_free_pct = 50.0;
        struct timeval bt; size_t l = sizeof(bt);
        double uptime = 0.0;
        if (sysctlbyname("kern.boottime",&bt,&l,nullptr,0)==0) {
            auto now = std::chrono::system_clock::now();
            auto bo = std::chrono::system_clock::from_time_t(bt.tv_sec);
            uptime = std::chrono::duration<double>(now - bo).count();
        }

        double score = computeScore();

        nlohmann::json m;
        m["ts"]           = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()
                            ).count();
        m["cpu_free_pct"] = load;
        m["mem_free_pct"] = mem_free_pct;
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
