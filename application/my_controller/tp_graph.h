#ifndef __MUL_TP_GRAPH_H__
#define __MUL_TP_GRAPH_H__

#include "uthash.h"
#include <stdlib.h>
#include "mul_common.h"

#ifndef __USE_MISC
#define __USE_MISC 1
#endif

//network adapter
#ifndef IF_NAME
#define IF_NAME "ens160"
#endif
 
//ethernet address len
#ifndef ETH_ADDR_LEN
#define ETH_ADDR_LEN 6
#endif

//the link node
typedef struct tp_link_t
{
    uint64_t key;//the link key
    uint64_t delay;//the link delay
    uint16_t all_bw;//all bandwidth
    uint16_t re_bw; //remain bandwidth
    uint32_t port_h;//the head port
    uint32_t port_n;//this node's port
    struct tp_link_t ** pprev;//point to the precursor node's next
    struct tp_link_t * next;//next link node
}tp_link;

//the port information of switch
typedef struct tp_sw_port_t
{
    uint32_t port_no;//port number
    uint8_t dl_hw_addr[ETH_ADDR_LEN];//data links MAC address
    struct tp_sw_port_t ** pprev;//point to the precursor node's next
    struct tp_sw_port_t * next;//next port node
}tp_sw_port;

//the information of switch
typedef struct tp_sw_t
{
    uint32_t key;//the node key(switch or host)
    tp_link * list_link;//the switch link head
    tp_sw_port * list_port;//the switch port head
    uint64_t delay;//the delay between controller and switch
    UT_hash_handle hh;//hash handler
}tp_sw;//switch hash table node

/**
 * return local ip address
 */
uint32_t tp_get_local_ip(void);

/**
 * get controller area from the database, and assign to the global variable controller_area
 * @ip_addr: local ip address that use to identify this controller
*/
void tp_get_area_from_db(uint32_t ip_addr);

/**
 * use the key to find switch node from tp_graph
 * @key: the node key(sw_dpid or host ip)
 * @tp_graph: topo_graph handler
 * @return: the corresponding tp_sw
 */
tp_sw * tp_find_sw(uint64_t key, tp_sw * tp_graph);

/**
 * add a switch node to tp_graph
 * @key: the node key(sw_dpid or host ip)
 * @tp_graph: topo_graph handler
 * @return: success added_topo_switch, fail NULL
 */
tp_sw * tp_add_sw(uint64_t key, tp_sw * tp_graph);

/**
 * add a switch port node to tp_graph
 * @sw: mul_switch the include the port information
 * @tp_graph: topo_graph handler
 * @return: success added_topo_switch_port, fail NULL
 */
tp_sw * tp_add_sw_port(mul_switch_t *sw, tp_sw * tp_graph);

/**
 * add a link to a switch link_head(the link switch_switch or switch_host but need to store twice)
 * @head: topo_switch_node
 * @n: the struct of link pointer
 */
void __tp_head_add_link(tp_sw *head, tp_link * n);

/**
 * store the link(switch_switch or host_switch)
 * @key: switch_dpid of host_ip
 * @port: link port
 * @tp_graph: topo_graph handler
 * @return: success 1, fail 0
 */
int tp_add_link(uint64_t key1, uint32_t port1, uint64_t key2, uint32_t port2, tp_sw * tp_graph);

/**
 * get a link from a switch link_head(correspond the __tp_head_add_link function)
 * @head: topo_switch_node
 * @key: node_key
 * @return: the link pointer
 */
tp_link * __tp_get_link_in_head(tp_link *head, uint64_t key);

/**
 * delete a link in a switch link_head
 * @del_n: the struct of link pointer
 */
void __tp_delete_link_in_head(tp_link *del_n);

/**
 * delete a link in topo
 * @key: two key of link_node
 * @tp_graph: topo_graph handler
 * @return: success 1, fail 0
 */
int tp_delete_link(uint64_t key1, uint64_t key2, tp_sw * tp_graph);

/**
 * delete a switch(host) in topo
 * @key: key of tp_node
 * @tp_graph: topo_graph handler
 * @return: success 1, fail 0
 */
int tp_delete_sw(uint64_t key, tp_sw * tp_graph);

/**
 * Destroys and cleans up topo.
 */
void tp_distory(tp_sw * tp_graph);

/**
 * add a port information in topo_switch
 * @head: topo_switch
 * @iter: the switch port list from mul_switch_t
 */
void __tp_sw_add_port(tp_sw *head, GSList * iter);

/**
 * delete a port from switch
 * @head: topo_switch
 * @port_no: port number
 */
void __tp_sw_del_port(tp_sw *head, uint32_t port_no);

/**
 * use the port number to find port node from topo_switch
 * @head: topo_switch
 * @port_no: port number
 * @return: the corresponding tp_sw_port
 */
tp_sw_port * __tp_sw_find_port(tp_sw *head, uint32_t port_no);

/**
 * Destroys and cleans up all port information from topo_switch.
 * @head: topo_switch
 */
void __tp_sw_del_all_port(tp_sw *head);

/**
 * set the delay between controller and switch
 * @key: topo_switch_dpid
 * @delay: unit(us)
 * @tp_graph: topo_graph handler
 * @return: success 1, fail 0
 */
int tp_set_sw_delay(uint64_t key, uint64_t delay, tp_sw * tp_graph);

// int tp_set_link_delay(uint64_t key1, uint64_t key2, uint64_t delay, tp_sw * tp_graph);
// int tp_set_link_all_bw(uint64_t key1, uint64_t key2, uint16_t all_bw, tp_sw * tp_graph);
// int tp_set_link_re_bw(uint64_t key1, uint64_t key2, uint16_t re_bw, tp_sw * tp_graph);
// prams is dalay or all_bw or re_bw(the name of struct tp_link member), 
// ret(return) is the result(uint16_t*)
// equal the three function above
#ifndef TP_SET_LINK
#define TP_SET_LINK(key1,key2,prams,tp_graph,ret)\
    tp_link * link_n1, *link_n2;\
    tp_sw *n1 = tp_find_sw(key1, tp_graph);\
    tp_sw *n2 = tp_find_sw(key2, tp_graph);\
    if(!n1 || !n2) ret = 0;\
    else\
    {\
        link_n1 = __tp_get_link_in_head(n1->list_link, key2);\
        link_n2 = __tp_get_link_in_head(n2->list_link, key1);\
        link_n1->prams = prams;\
        link_n2->prams = prams;\
        ret = 1;\
    }
#endif

/**
 * get the delay between controller and switch
 * @key: topo_switch_dpid
 * @tp_graph: topo_graph handler
 * @return: delay(us)
 */
uint64_t tp_get_sw_delay(uint64_t key, tp_sw * tp_graph);

// uint64_t tp_get_link_delay(uint64_t key1, uint64_t key2, tp_sw * tp_graph);
// uint16_t tp_get_link_all_bw(uint64_t key1, uint64_t key2, tp_sw * tp_graph);
// uint16_t tp_get_link_re_bw(uint64_t key1, uint64_t key2, tp_sw * tp_graph);
// prams is dalay or all_bw or re_bw(the name of struct tp_link member), 
// ret(return) is the result(uint64_t*)
// equal the three function above
#ifndef TP_GET_LINK
#define TP_GET_LINK(key1,key2,prams,tp_graph,ret)\
    tp_link * link_n;\
    tp_sw *n = tp_find_sw(key1, tp_graph);\
    if(!n) ret = 0;\
    else\
    {\
        link_n = __tp_get_link_in_head(n->list_link, key2);\
        ret = link_n->prams;\
    }
#endif

#endif