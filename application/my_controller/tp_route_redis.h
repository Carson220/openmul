#ifndef __MUL_TP_ROUTE_H__
#define __MUL_TP_ROUTE_H__

#include "tp_graph.h"
#include "mul_common.h"

// #define MAXNUM 0xffffffff // max node number
#define INF 0xffffffff // init minimum delay

// edge structure
struct edge{
    int32_t node_id; // other sw key
    int8_t port1; // self port
    int8_t port2; // other port
    int64_t delay; // edge weight
    struct edge* next;
};

// node structure
struct node{
    int32_t node_id; // self sw key
    int32_t rt_pre; // route pre node 
    int64_t dist; // sum distance
    bool flag; // false = temp mark 
    struct edge* next;
};

// hash node structure
struct hash_node{
    int32_t node_id;                    /* key */
    struct node *value;
    UT_hash_handle hh;         /* makes this structure hashable */
};

/**
 * the function of add hash node
 * @sw_key: sw_key
 * @node: node structure
 */
void add_node(uint32_t sw_key, struct node *node);

/**
 * the function of find hash node by sw_key
 * @node_id: sw_key
 * @return: hash node
 */
struct hash_node *find_node(int32_t node_id)

/**
 * the function of del hash node
 * @node: hash node structure
 */
void del_node(struct hash_node * node);

/**
 * the function of del all hash node
 */
void del_all_node(void);

/**
 * the function of topo creation by redis
 * @return: success 1, fail 0
 */
int tp_create(void);

/**
 * the function of ip route and set flow_table
 * @sw_src: source switch global key
 * @ip_src: ip source
 * @ip_dst: ip destination
 * @return: success 1, fail 0
 */
int tp_rt_redis_ip(uint32_t sw_src, uint32_t ip_src, uint32_t ip_dst);

#endif
