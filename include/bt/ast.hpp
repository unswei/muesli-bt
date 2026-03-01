#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bt {

using node_id = std::uint32_t;

enum class node_kind {
    seq,
    sel,
    invert,
    repeat,
    retry,
    cond,
    act,
    succeed,
    fail,
    running,
    plan_action,
    vla_request,
    vla_wait,
    vla_cancel,
    mem_seq,
    mem_sel,
    async_seq,
    reactive_seq,
    reactive_sel
};

enum class arg_kind {
    nil,
    boolean,
    integer,
    floating,
    symbol,
    string
};

struct arg_value {
    arg_kind kind = arg_kind::nil;
    bool bool_v = false;
    std::int64_t int_v = 0;
    double float_v = 0.0;
    std::string text;
};

struct node {
    node_kind kind = node_kind::seq;
    node_id id = 0;
    std::vector<node_id> children;

    std::string leaf_name;
    std::vector<arg_value> args;

    std::int64_t int_param = 0;
};

struct definition {
    std::vector<node> nodes;
    node_id root = 0;
};

}  // namespace bt
