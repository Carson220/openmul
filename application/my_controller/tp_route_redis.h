#ifndef __MUL_TP_ROUTE_H__
#define __MUL_TP_ROUTE_H__

#include "tp_graph.h"
#include "mul_common.h"

#define MAXNUM 0xffffffff // max node number
#define INF 0xffffffff // init minimum delay

// edge structure
struct edge{
    int32_t node_id; // other sw key
    int8_t port1; // self port
    int8_t port2; // other port
    int64_t delay; // edge weight
    struct edge* next;
}

// node structure
struct node{
    int32_t node_id; // self sw key
    int32_t rt_pre; // route pre node 
    int64_t dist; // sum distance
    bool flag; // false = temp mark 
    struct edge* next;
}

/**
 * the function of ip route and set flow_table
 * @nw_src: ip source address
 * @nw_dst: ip destination
 * @return: success 1, fail 0
 */
int tp_rt_redis_ip(uint32_t nw_src, uint32_t nw_dst);

#endif
