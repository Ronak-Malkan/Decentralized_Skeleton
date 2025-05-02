
#include "server.h"
#include "thread_safe_queue.h"
#include "file_logger.h"

#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/support/status.h>
#include <google/protobuf/empty.pb.h>
#include "mini3.grpc.pb.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>
#include <algorithm>
#include <random>
#include <vector>
#include <utility>
#include <string>
#include <filesystem>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>

using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;
using google::protobuf::Empty;
using namespace mini3;

static double getMemoryFreeFraction() {
#ifdef __APPLE__
    uint64_t mem_total = 0;
    size_t len = sizeof(mem_total);
    if (sysctlbyname("hw.memsize", &mem_total, &len, nullptr, 0) != 0) {
        return 0.5;
    }
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics_data_t vmstat;
    if (host_statistics(mach_host_self(),
                        HOST_VM_INFO,
                        reinterpret_cast<host_info_t>(&vmstat),
                        &count) != KERN_SUCCESS) {
        return 0.5;
    }
    uint64_t free_bytes = uint64_t(vmstat.free_count + vmstat.inactive_count)
                          * uint64_t(sysconf(_SC_PAGESIZE));
    return std::clamp(double(free_bytes) / double(mem_total), 0.0, 1.0);
#else
    return 0.5;
#endif
}

// Used chatgpt for calculating factors needed for the score, though the score formula itself was defined by our team itself.
//------------------------------------------------------------------------------
// Score = 40% CPU-free + 30% queue-depth + 20% mem-free + 10% uptime, plus jitter
double Server::computeScore() {
    // --- CPU free ---
    double load = 0.0;
    if (getloadavg(&load, 1) != -1) {
        unsigned cores = std::thread::hardware_concurrency();
        double busy = std::min(load / double(cores), 1.0);
        load = 1.0 - busy;
    } else {
        load = 0.5;
    }

    // --- Memory free ---
    double mem_free = getMemoryFreeFraction();

    // --- Heartbeat‐queue depth fraction (older heartbeats pending) ---
    double qlen = double(hb_queue_.size());
    double q_frac = 1.0 - std::min(qlen, double(MAX_QUEUE)) / double(MAX_QUEUE);

    // --- Uptime fraction ---
    double up_frac = 0.0;
    {
#ifdef __APPLE__
        struct timeval boottime{};
        size_t sz = sizeof(boottime);
        if (sysctlbyname("kern.boottime", &boottime, &sz, nullptr, 0) == 0) {
            auto now = std::chrono::system_clock::now();
            auto boot = std::chrono::system_clock::from_time_t(boottime.tv_sec);
            double secs = std::chrono::duration<double>(now - boot).count();
            up_frac = std::min(secs, 86400.0) / 86400.0;
        }
#endif
    }

    double base =
         0.40 * load
       + 0.30 * q_frac
       + 0.20 * mem_free
       + 0.10 * up_frac;

    // jitter ±0.005 to avoid ties
    static thread_local std::mt19937_64 gen{std::random_device{}()};
    std::uniform_real_distribution<double> jitter(-0.005, +0.005);
    return base + jitter(gen);
}

//------------------------------------------------------------------------------
// Constructor / Destructor
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
        auto channel = grpc::CreateChannel(addr,
                                           grpc::InsecureChannelCredentials());
        stubs_.emplace_back(Mini3Service::NewStub(channel));
        stub_addrs_.push_back(addr);
    }
    logger_->log("neighbors_count", {{"neighbors", neighbors_.size()}});
    logger_->log("stubs_count",     {{"stubs",     stub_addrs_.size()}});
}

Server::~Server() {
    shutdown();
}


//------------------------------------------------------------------------------
// run / shutdown
void Server::run() {
    running_ = true;
    logger_->log("threads_starting");
    //below threads generation and joining was suggested by chatgpt
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

void Server::shutdown() {
    running_ = false;
    if (grpc_server_) grpc_server_->Shutdown();
    logger_->log("server_shutdown");
}

//------------------------------------------------------------------------------
// Used chatgpt for below function
// listenerLoop: synchronous SubmitTask + heartbeats→queue 
void Server::listenerLoop() {
    logger_->log("thread_started", {{"thread","listener"}});

    class ServiceImpl final : public Mini3Service::Service {
        Server* srv_;
        FileLogger& log_;
    public:
        explicit ServiceImpl(Server* s)
          : srv_(s), log_(*s->logger_) {}

        Status SendHeartbeat(ServerContext*, const Heartbeat* req, Empty*) override {
            log_.log("heartbeat_received", {
                {"from", req->from()},
                {"gossip_size", req->gossip_size()}
            });
            srv_->hb_queue_.push(*req);
            return Status::OK;
        }

        Status SubmitTask(ServerContext*,
                          const TaskRequest* req,
                          TaskResponse*      resp) override
        {
            log_.log("task_request_received", {{"task", req->task_id()}});

            // Build sorted candidate list
            std::vector<std::pair<std::string,double>> candidates;
            candidates.emplace_back(
                srv_->node_id_, srv_->computeScore()
            );
            for (auto& [id,e] : srv_->peer_info_) {
                candidates.emplace_back(id, e.score);
            }
            std::sort(candidates.begin(),
                      candidates.end(),
                      [](auto &a, auto &b){ return a.second > b.second; });

            // Try each candidate in order
            log_.log("task_candidates", {
                {"task", req->task_id()},
                {"candidates", candidates.size()}
            });
            for (auto& [id,score] : candidates) {
                log_.log("task_candidate", {
                    {"task", req->task_id()},
                    {"peer", id},
                    {"score", score}
                });
                if (id == srv_->node_id_) {
                    log_.log("task_local", {{"task", req->task_id()}});
                    srv_->processLocalSync(*req, resp);
                    return Status::OK;
                }
                // forward
                auto viaIt = srv_->next_hop_.find(id);
                if (viaIt == srv_->next_hop_.end()) continue;
                auto via = viaIt->second;
                auto it = std::find(
                    srv_->stub_addrs_.begin(),
                    srv_->stub_addrs_.end(),
                    via
                );
                if (it == srv_->stub_addrs_.end()) continue;
                size_t idx = std::distance(
                    srv_->stub_addrs_.begin(), it
                );

                ClientContext fwd_ctx;
                fwd_ctx.set_deadline(
                    std::chrono::system_clock::now()
                  + std::chrono::seconds(5)
                );
                TaskResponse tmp;
                Status s = srv_->stubs_[idx]->SubmitTask(
                    &fwd_ctx, *req, &tmp
                );
                if (s.ok()) {
                    *resp = tmp;
                    log_.log("task_forward_ok", {
                        {"task",      req->task_id()},
                        {"responder", tmp.result()}
                    });
                    return Status::OK;
                }
                log_.log("task_forward_fail", {
                    {"task", req->task_id()},
                    {"to",    via},
                    {"error", s.error_message()}
                });
            }

            log_.log("task_drop", {{"task", req->task_id()}});
            return Status(
                StatusCode::INTERNAL,
                "all forwarding attempts failed"
            );
        }
    };

    ServiceImpl service(this);
    ServerBuilder builder;
    builder.AddListeningPort(
        listen_addr_,
        grpc::InsecureServerCredentials()
    );
    builder.RegisterService(&service);
    grpc_server_ = builder.BuildAndStart();
    logger_->log("grpc_listening", {{"addr", listen_addr_}});
    grpc_server_->Wait();
}

//------------------------------------------------------------------------------
// processorLoop: only heartbeats → peer_info_
void Server::processorLoop() {
    logger_->log("thread_started", {{"thread","processor"}});
    while (running_) {
        auto hb = hb_queue_.wait_and_pop();
        updatePeerInfo(hb, hb.from());
    }
    logger_->log("thread_exiting", {{"thread","processor"}});
}

void Server::updatePeerInfo(const Heartbeat& hb,
                            const std::string& sender)
{
    auto now = std::chrono::steady_clock::now();    
    peer_info_[sender] = PeerEntry{hb.my_score(), sender, 1, now};
    for (auto& pi : hb.gossip()) {
        int nh = pi.hops() + 1;
        auto it = peer_info_.find(pi.node_id());
        bool upd = it == peer_info_.end()
                || nh < it->second.hops
                || (nh == it->second.hops && pi.score() > it->second.score);
        if (upd) {
            peer_info_[pi.node_id()] = PeerEntry{pi.score(),
                                                 sender, nh, now};
        }
    }
    logger_->log("peer_update", {
        {"sender", sender},
        {"known_peers", peer_info_.size()}
    });
}

//------------------------------------------------------------------------------
// Local work
void Server::processLocalSync(const TaskRequest& req,
                              TaskResponse*    resp)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    resp->set_task_id(req.task_id());
    resp->set_result("Processed by " + node_id_);
    logger_->log("task_complete", {{"task", req.task_id()}});
}

//------------------------------------------------------------------------------
// heartbeatLoop
void Server::heartbeatLoop() {
    logger_->log("thread_started", {{"thread","heartbeat"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    while (running_) {
        double score = computeScore();
        Heartbeat hb;
        hb.set_from(node_id_);
        hb.set_my_score(score);

        // for (auto& [peer_id,entry] : peer_info_) {
        //     if (entry.hops <= 3) {
        //         auto* pi = hb.add_gossip();
        //         pi->set_node_id(peer_id);
        //         pi->set_score(entry.score);
        //         pi->set_hops(entry.hops);
        //     }
        // }

        logger_->log("heartbeat_sent", {
            {"score", score},
            {"gossip_size", hb.gossip_size()}
        });

        for (size_t i = 0; i < stubs_.size(); ++i) {
            auto stub = stubs_[i].get();
            auto addr = stub_addrs_[i];
            Heartbeat copy = hb;
            std::thread([stub, addr, copy, logger = logger_.get()]() mutable {
                ClientContext ctx;
                ctx.set_wait_for_ready(true);
                Empty rep;
                Status s = stub->SendHeartbeat(&ctx, copy, &rep);
                if (s.ok()) {
                    logger->log("heartbeat_ok", {{"to", addr}});
                } else {
                    logger->log("heartbeat_fail", {
                        {"to", addr},
                        {"error", s.error_message()}
                    });
                }
            }).detach();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    logger_->log("thread_exiting", {{"thread","heartbeat"}});
}

//------------------------------------------------------------------------------
// routingLoop
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

//------------------------------------------------------------------------------
// metricsLoop
void Server::metricsLoop() {
    logger_->log("thread_started", {{"thread","metrics"}});
    while (running_) {
        double load = 0.0;
        if (getloadavg(&load,1) != -1) {
            unsigned c = std::thread::hardware_concurrency();
            double u = std::min(load/double(c), 1.0);
            load = (1.0 - u) * 100.0;
        }
        double mem_free_pct = getMemoryFreeFraction() * 100.0;
        struct timeval bt{};
        size_t l = sizeof(bt);
        double uptime = 0.0;
        if (sysctlbyname("kern.boottime",&bt,&l,nullptr,0)==0) {
            auto now = std::chrono::system_clock::now();
            auto bo  = std::chrono::system_clock::from_time_t(bt.tv_sec);
            uptime = std::chrono::duration<double>(now-bo).count();
        }

        nlohmann::json m;
        m["ts"]           =
          std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now()
               .time_since_epoch()
           ).count();
        m["cpu_free_pct"] = load;
        m["mem_free_pct"] = mem_free_pct;
        m["uptime_sec"]   = uptime;
        m["score"]        = computeScore();

        {
            std::lock_guard<std::mutex> lk(metrics_mtx_);
            metrics_file_ << m.dump() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    logger_->log("thread_exiting", {{"thread","metrics"}});
}
