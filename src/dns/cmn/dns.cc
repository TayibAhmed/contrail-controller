/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign.hpp>
#include <base/logging.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <cmn/dns.h>
#include <cmn/buildinfo.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "ifmap/client/config_client_manager.h"

using namespace std;
EventManager *Dns::event_mgr_;
DnsManager *Dns::dns_mgr_;
DnsConfigManager *Dns::dns_config_mgr_;
XmppServer *Dns::xmpp_server_;
uint32_t Dns::xmpp_srv_port_;
DnsAgentXmppChannelManager *Dns::agent_xmpp_channel_mgr_;
std::string Dns::host_name_;
std::string Dns::prog_name_;
std::string Dns::collector_;
std::string Dns::self_ip_;
uint32_t Dns::http_port_;
uint32_t Dns::dns_port_ = ContrailPorts::DnsServerPort();

bool Dns::GetVersion(string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::Dns, BuildInfo, build_info_str);
}

void Dns::SetTaskSchedulingPolicy() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskPolicy exclude_all;
    const char *config_exclude_list[] = {
        "dns::Config",
        "dns::BindStatus",
        "db::DBTable",
        "db::IFMapTable",
        "io::ReaderTask",
        "timer::TimerTask",
        "bgp::Config",
        "xmpp::StateMachine",
        "ifmap::StateMachine",
        "sandesh::RecvQueue",
        "http::RequestHandlerTask",
        "dns::NamedSndRcv"
    };
    int arraysize = sizeof(config_exclude_list) / sizeof(char *);
    for (int i = 0; i < arraysize; ++i) {
        int task_id = scheduler->GetTaskId(config_exclude_list[i]);
        exclude_all.push_back(TaskExclusion(task_id));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("dns::Config"), exclude_all);
    scheduler->SetPolicy(scheduler->GetTaskId("bgp::Config"), exclude_all);

    const char *bindstatus_exclude_list[] = {
        "io::ReaderTask",
        "bgp::Config",
        "xmpp::StateMachine",
        "sandesh::RecvQueue",
        "http::RequestHandlerTask",
    };
    arraysize = sizeof(bindstatus_exclude_list) / sizeof(char *);
    TaskPolicy bindstatus_exclude;
    for (int i = 0; i < arraysize; ++i) {
        int task_id = scheduler->GetTaskId(bindstatus_exclude_list[i]);
        bindstatus_exclude.push_back(TaskExclusion(task_id));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("dns::BindStatus"), bindstatus_exclude);

    TaskPolicy exclude_io = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("io::ReaderTask")))
        (TaskExclusion(scheduler->GetTaskId("sandesh::RecvQueue")))
        (TaskExclusion(scheduler->GetTaskId("http::RequestHandlerTask")));
    scheduler->SetPolicy(scheduler->GetTaskId("xmpp::StateMachine"),
                         exclude_io);

    // Policy for cassandra::Reader Task.
    TaskPolicy cassadra_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::init")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::FQNameReader")));
    for (int idx = 0; idx < ConfigClientManager::GetNumConfigReader(); ++idx) {
        cassadra_reader_policy.push_back(
        TaskExclusion(scheduler->GetTaskId("cassandra::ObjectProcessor"), idx));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::Reader"),
        cassadra_reader_policy);

    // Policy for cassandra::ObjectProcessor Task.
    TaskPolicy cassadra_obj_process_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::init")));
    for (int idx = 0; idx < ConfigClientManager::GetNumConfigReader(); ++idx) {
        cassadra_obj_process_policy.push_back(
                 TaskExclusion(scheduler->GetTaskId("cassandra::Reader"), idx));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::ObjectProcessor"),
        cassadra_obj_process_policy);

    // Policy for cassandra::FQNameReader Task.
    TaskPolicy fq_name_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::init")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::Reader")));
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::FQNameReader"),
        fq_name_reader_policy);

    // Policy for cassandra::init process
    TaskPolicy cassandra_init_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("amqp::RabbitMQReader")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::ObjectProcessor")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::FQNameReader")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::Reader")));
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::init"),
        cassandra_init_policy);
}
