#include "tp_route_redis.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "db_wr.h"

extern tp_sw * tp_graph;
extern tp_swdpid_glabolkey * key_table;
struct hash_node * hash_map = NULL;

int32_t node_sw_num = 0;

void add_node(uint32_t sw_key, struct node *node)
{
    struct hash_node * s = NULL;

    s = (struct hash_node *)malloc(sizeof *s);
    s->node_id = sw_key;
    s->value = node;
    HASH_ADD_INT(hash_map, node_id, s); /* node_id: name of key field */

}

struct hash_node * find_node(int32_t node_id)
{
    struct hash_node *s = NULL;

    HASH_FIND_INT(hash_map, &node_id, s);  /* s: output pointer */
    return s;
}

void del_node(struct hash_node * node)
{
    HASH_DEL(hash_map, node);  /* node: pointer to deletee */
    free(node);             /* optional; it's up to you! */
}

void del_all_node(void) 
{
  struct hash_node *current_node, *tmp;
  struct edge *head, *next;

  HASH_ITER(hh, hash_map, current_node, tmp) {
    head = current_node->value->next;
    for(; head != NULL; )
    {
        next = head->next;
        // c_log_debug("free sw %x edge head node_id %x, delay %lu us", current_node->value->node_id, head->node_id, head->delay);
        free(head);
        head = next;
    }
    free(current_node->value);
    // c_log_debug("free sw %x node", current_node->value->node_id);
    HASH_DEL(hash_map, current_node);  /* delete; hash_map advances to next */
    free(current_node);             /* optional- if you want to free  */
    // c_log_debug("free hash node");
  }
}

int tp_create(void)
{
    char cmd[CMD_MAX_LENGHT] = {0};
    uint8_t port1, port2; // port id
    uint32_t sw1, sw2; // sw key id
    uint64_t port, delay;
    redisContext *context;
    redisReply *reply;
    struct edge *link_sw, *tmp;
    struct node *node;
    int i = 0;

    /*组装Redis命令*/
    snprintf(cmd, CMD_MAX_LENGHT, "hgetall link_delay");
    // for(int i=0;cmd[i]!='\0';i++)
    // 	printf("%c",cmd[i]);
    // printf("\n");

    /*连接redis*/
    context = redisConnect(REDIS_SERVER_IP, REDIS_SERVER_PORT);
    if (context->err)
    {
        redisFree(context);
        printf("%d connect redis server failure:%s\n", __LINE__, context->errstr);
        return 0;
    }
    printf("connect redis server success\n");

    /*执行redis命令*/
    reply = (redisReply *)redisCommand(context, cmd);
    if (NULL == reply)
    {
        printf("%d execute command:%s failure\n", __LINE__, cmd);
        redisFree(context);
        return 0;
    }

    // 输出查询结果 
    // printf("%d,%lu\n",reply->type,reply->elements);
    printf("element num = %lu\n",reply->elements);
    for(i=0;i < reply->elements;i++)
    {
        if(i%2 ==0)// port
        {
            printf("link %s delay: ",reply->element[i]->str);
            port = atol(reply->element[i]->str);
            // c_log_debug("port = %lu", port);
            
            sw1 = (uint32_t)((port & 0xffffff0000000000) >> 32);
            // c_log_debug("sw1 = %x", sw1);
            sw2 = (uint32_t)(port & 0x00000000ffffff00);
            // c_log_debug("sw1 = %x", sw2);
            port1 = (uint8_t)((port & 0x000000ff00000000) >> 32);
            // c_log_debug("port1 = %u", port1);
            port2 = (uint8_t)(port & 0x00000000000000ff);
            // c_log_debug("port2 = %u", port2);

            if(find_node(sw1) == NULL)
            {
                node = (struct node*)malloc(sizeof(struct node));
                node->node_id = sw1;
                node->dist = INF;
                node->flag = false;
                node->rt_pre = 0;
                node->next = NULL;
                add_node(sw1, node);
                node_sw_num++;
            }
            if(find_node(sw2) == NULL)
            {
                node = (struct node*)malloc(sizeof(struct node));
                node->node_id = sw2;
                node->dist = INF;
                node->flag = false;
                node->rt_pre = 0;
                node->next = NULL;
                add_node(sw2, node);
                node_sw_num++;
            }
        }
        else// delay
        {
            printf("%s us\n",reply->element[i]->str);
            delay = atol(reply->element[i]->str);

            // construct sw<->sw topo
            // you can skip "for" to save time
            // fix 无向图 避免重复存储边
            tmp = find_node(sw1)->value->next;
            for(; tmp != NULL; )// update sw-sw min delay
            {
                if(tmp->node_id == sw2 && delay <= tmp->delay)
                {
                    tmp->delay = delay;
                    tmp->port1 = port1;
                    tmp->port2 = port2;
                    break;
                }
                tmp = tmp->next;
            }
            if(tmp == NULL)
            {
                link_sw = (struct edge*)malloc(sizeof(struct edge));
                link_sw->node_id = sw2;
                link_sw->delay = delay;
                link_sw->port1 = port1;
                link_sw->port2 = port2;
                link_sw->next = find_node(sw1)->value->next;
                find_node(sw1)->value->next = link_sw;
                // c_log_debug("sw %x link node_id = %x, delay = %lu us", sw1, find_node(sw1)->value->next->node_id, find_node(sw1)->value->next->delay);
            }

            tmp = find_node(sw2)->value->next;
            for(; tmp != NULL; )// update sw-sw min delay
            {
                if(tmp->node_id == sw1 && delay <= tmp->delay)
                {
                    tmp->delay = delay;
                    tmp->port1 = port2;
                    tmp->port2 = port1;
                    break;
                }
                tmp = tmp->next;
            }
            if(tmp == NULL)
            {
                link_sw = (struct edge*)malloc(sizeof(struct edge));
                link_sw->node_id = sw1;
                link_sw->delay = delay;
                link_sw->port1 = port2;
                link_sw->port2 = port1;
                link_sw->next = find_node(sw2)->value->next;
                find_node(sw2)->value->next = link_sw;
                // c_log_debug("sw %x link node_id = %x, delay = %lu us", sw2, find_node(sw2)->value->next->node_id, find_node(sw2)->value->next->delay);
            }
        }
    }

    freeReplyObject(reply);
    redisFree(context);
    return 1;
}

int route_lookup(uint32_t sw_src, uint32_t ip_src, uint32_t ip_dst)
{
    char cmd[CMD_MAX_LENGHT] = {0};
    uint64_t ip = (((uint64_t)ip_src) << 32) + ip_dst;
    int route_is_exist = 0;
    int32_t i;

    redisContext *context;
    redisReply *reply;

    uint32_t out_sw_port, outsw, outport; // sw_key+port sw_key port
    tp_sw * dst_node; // out switch structure
    struct flow fl;
    struct flow mask;
    mul_act_mdata_t mdata;

    /*组装Redis命令*/
    snprintf(cmd, CMD_MAX_LENGHT, "lrange %lu 0 -1", ip);
    // for(int i=0;cmd[i]!='\0';i++)
    // 	printf("%c",cmd[i]);
    // printf("\n");

    /*连接redis*/
    context = redisConnect(REDIS_SERVER_IP, REDIS_SERVER_PORT);
    if (context->err)
    {
        redisFree(context);
        printf("%d connect redis server failure:%s\n", __LINE__, context->errstr);
        return 0;
    }
    printf("connect redis server success\n");

    /*执行redis命令*/
    reply = (redisReply *)redisCommand(context, cmd);
    if (NULL == reply)
    {
        printf("%d execute command:%s failure\n", __LINE__, cmd);
        redisFree(context);
        return 0;
    }

    // 输出查询结果 
    // printf("%d,%lu\n",reply->type,reply->elements);
    printf("element num = %lu\n",reply->elements);
    if(reply->elements == 0) return 0;
    for(i=0;i < reply->elements;i++)
    {
        printf("out_sw_port: %s",reply->element[i]->str);
        out_sw_port = atoi(reply->element[i]->str);
        outsw = (out_sw_port & 0xffffff00);
        if(outsw == sw_src) route_is_exist = 1;
        if(route_is_exist)
        {
            // set flow_table
            dst_node = tp_find_sw(outsw);
            outport = (out_sw_port & 0x000000ff);
            if(dst_node != NULL) // store in local
            {
                //流表下发
                c_log_debug("set flow table");
                memset(&fl, 0, sizeof(fl));
                memset(&mdata, 0, sizeof(mdata));
                of_mask_set_dc_all(&mask);
            
                fl.ip.nw_src = ip_src;
                of_mask_set_nw_src(&mask, 32);
                fl.ip.nw_dst = ip_dst;
                of_mask_set_nw_dst(&mask, 32);
                
                // fix set flow table error
                fl.dl_type = htons(ETH_TYPE_IP);
                of_mask_set_dl_type(&mask);

                mul_app_act_alloc(&mdata);
                mul_app_act_set_ctors(&mdata, dst_node->sw_dpid);
                mul_app_action_output(&mdata, outport); 


                mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dst_node->sw_dpid, &fl, &mask,
                                    0xffffffff, mdata.act_base, mul_app_act_len(&mdata),
                                    0, 0, C_FL_PRIO_EXM, C_FL_ENT_NOCACHE);
            }
        }
    }

    freeReplyObject(reply);
    redisFree(context);
    if(route_is_exist) return 1;
    else return 0;
}

int tp_rt_redis_ip(uint32_t sw_src, uint32_t ip_src, uint32_t ip_dst)
{
    struct edge *tmp;
    uint64_t min_dst; // minimum distance
    int32_t min_node; // minimum sw_key
    int32_t i;
    struct hash_node *s;

    uint32_t outsw; // out switch
    tp_sw * dst_node; // out switch structure
    uint32_t outport; // out switch port
    struct flow fl;
    struct flow mask;
    mul_act_mdata_t mdata;
    
    // get start and end info
    uint32_t port_end;
    uint32_t sw_start, sw_end;
    port_end = Get_Pc_Sw_Port(ip_dst);
    sw_start = sw_src;
    sw_end = (port_end & 0xffffff00);

    // lookup route from redis and set flow_table
    if(route_lookup(sw_src, ip_src, ip_dst))
        return 1;


    // create topo by redis
    tp_create();
    // c_log_debug("finish to create topo");

    // calculate route sw-sw
    find_node(sw_start)->value->dist = 0;
    if(sw_start != sw_end)
    {
        tmp = find_node(sw_start)->value->next;
        for(; tmp != NULL; tmp = tmp->next)//从起点开始 
        {
            find_node(tmp->node_id)->value->dist = tmp->delay;
            find_node(tmp->node_id)->value->rt_pre = sw_start; // update pre_node
        }
        find_node(sw_start)->value->dist = 0;
        find_node(sw_start)->value->flag = 1;
        
        for(i = 0;i < node_sw_num;i ++)// 循环N次
        {
            min_node = 0;
            min_dst = INF;

            for (s = hash_map; s != NULL; s = s->hh.next) //寻找下一个固定标号
            {
                if(!s->value->flag && s->value->dist < min_dst)
                {
                    min_dst = s->value->dist;
                    min_node = s->node_id;
                    c_log_debug("update min_node %x, min_dst %lu us", find_node(min_node)->value->node_id, find_node(min_node)->value->dist);
                }
            }

            if(min_node == 0) break; // 找不到最短路径 
            find_node(min_node)->value->flag = 1; // 改为固定标号
            c_log_debug("min_node %x", find_node(min_node)->value->node_id);
            if(min_node == sw_end) break; // 找到最短路径

            tmp = find_node(min_node)->value->next;
            for(; tmp != NULL; tmp = tmp->next) // 更新相邻节点标号值 
            {
                if(!find_node(tmp->node_id)->value->flag && min_dst + tmp->delay < find_node(tmp->node_id)->value->dist)
                {
                    find_node(tmp->node_id)->value->dist = min_dst + tmp->delay;
                    find_node(tmp->node_id)->value->rt_pre = min_node;
                    c_log_debug("sw %x update pre_node %x, dst %lu us", tmp->node_id, \
                        find_node(tmp->node_id)->value->rt_pre, find_node(tmp->node_id)->value->dist);
                }
            }

        }
        c_log_debug("finish to calculate route");
        if(min_node == 0)
        {
            printf("no path\n");
            return 0;
        }
        else 
            printf("ip route delay: %lu us\n",find_node(sw_end)->value->dist);
    }
    else
        printf("ip route delay: %lu us\n",find_node(sw_end)->value->dist);

    // set flow-table
    Clr_Route(ip_src, ip_dst);
    outsw = sw_end;
    dst_node = tp_find_sw(outsw);
    outport = (port_end & 0x000000ff);
    c_log_debug("outsw = %x, outport = %x", outsw, outport);

    do{
        if(dst_node != NULL) // store in local
        {
            //流表下发
            c_log_debug("set flow table");
            memset(&fl, 0, sizeof(fl));
            memset(&mdata, 0, sizeof(mdata));
            of_mask_set_dc_all(&mask);
        
            fl.ip.nw_src = ip_src;
            of_mask_set_nw_src(&mask, 32);
            fl.ip.nw_dst = ip_dst;
            of_mask_set_nw_dst(&mask, 32);
            
            // fix set flow table error
            fl.dl_type = htons(ETH_TYPE_IP);
            of_mask_set_dl_type(&mask);

            mul_app_act_alloc(&mdata);
            mul_app_act_set_ctors(&mdata, dst_node->sw_dpid);
            mul_app_action_output(&mdata, outport); 


            mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dst_node->sw_dpid, &fl, &mask,
                                0xffffffff, mdata.act_base, mul_app_act_len(&mdata),
                                0, 0, C_FL_PRIO_EXM, C_FL_ENT_NOCACHE);
        }

        // write into redis
        Set_Route(ip_src, ip_dst, outsw + outport);

        if(find_node(outsw)->value->rt_pre != 0)
        {
            c_log_debug("sw %x pre_node: %x", find_node(outsw)->value->node_id, find_node(outsw)->value->rt_pre);
            tmp = find_node(outsw)->value->next;
            for(; tmp != NULL; )
            {
                if(tmp->node_id == find_node(outsw)->value->rt_pre)
                {
                    outport = tmp->port2;
                    break;
                }
                tmp = tmp->next;
            }

            outsw = find_node(outsw)->value->rt_pre;
            c_log_debug("outsw = %x, outport = %x", outsw, outport);
            dst_node = tp_find_sw(outsw);
        }
        else
            break;
        
    }while(1);

    c_log_debug("finish to set flow table");
    mul_app_act_free(&mdata);

    // del topo (hash map & node)
    del_all_node();
    c_log_debug("free all hash node to del topo");
    node_sw_num = 0;
    return 1;
}