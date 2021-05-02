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
    HASH_DEL(hash_map, current_node);  /* delete; hash_map advances to next */
    head = current_node->value->next;
    next = head->next;
    for(; head != NULL; )
    {
        free(head);
        head = next;
        next = head->next;
    }
    free(current_node);             /* optional- if you want to free  */
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
            printf("%s:",reply->element[i]->str);
            port = atoi(reply->element[i]->str);
            
            sw1 = (uint32_t)((port & 0xffffff0000000000) >> 32);
            sw2 = (uint32_t)(port & 0x00000000ffffff00);
            port1 = (uint8_t)((port & 0x000000ff00000000) >> 32);
            port2 = (uint8_t)(port & 0x00000000000000ff);

            if(find_node(sw1) == NULL)
            {
                node = (struct node*)malloc(sizeof(struct node));
                node->node_id = sw1;
                node->dist = INF;
                node->flag = false;
                node->rt_pre = 0;
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
                add_node(sw2, node);
                node_sw_num++;
            }
        }
        else// delay
        {
            printf("%s\n",reply->element[i]->str);
            delay = atoi(reply->element[i]->str);

            // construct sw<->sw topo
            // you can skip "for" to save time
            tmp = find_node(sw1)->value->next;
            for(; tmp != NULL; )// update sw-sw min delay
            {
                if(tmp->node_id == sw2 && delay < tmp->delay)
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
            }

            tmp = find_node(sw2)->value->next;
            for(; tmp != NULL; )// update sw-sw min delay
            {
                if(tmp->node_id == sw1 && delay < tmp->delay)
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
            }
        }
    }

    freeReplyObject(reply);
    redisFree(context);
    return 1;
}

int tp_rt_redis_ip(uint32_t sw_src, uint32_t ip_src, uint32_t ip_dst)
{
    struct edge *tmp;
    uint64_t min_dst; // minimum distance
    int32_t min_node; // minimum sw_key
    int32_t pre_node; // pre node sw_key
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

    // create topo by redis
    tp_create();

    // calculate route sw-sw
    tmp = find_node(sw_start)->value->next;
    for(; tmp != NULL; )//从起点开始 
	{
        find_node(tmp->node_id)->value->dist = tmp->delay;
        find_node(sw_start)->value->next = find_node(sw_start)->value->next->next;
	}
    find_node(sw_start)->value->dist = 0;
    find_node(sw_start)->value->flag = 1;

    pre_node = sw_start;
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
            }
        }

        if(min_node == 0) break;// 找不到最短路径 

        find_node(min_node)->value->flag = 1; // 改为固定标号
        find_node(min_node)->value->rt_pre = pre_node;
        pre_node = min_node;

        if(min_node == sw_end) break;// 找到最短路径

        tmp = find_node(min_node)->value->next;
        for(; tmp != NULL; )// 更新相邻节点标号值 
        {
            if(!find_node(tmp->node_id)->value->flag && min_dst + tmp->delay < find_node(tmp->node_id)->value->dist)
            {
                find_node(tmp->node_id)->value->dist = min_dst + tmp->delay;
            }
        }

    }
	if(min_node == 0)
    {
        printf("no path\n");
        return 0;
    }
	else 
        printf("ip route delay: %lu us\n",find_node(sw_end)->value->dist);

    // set flow-table
    outsw = sw_end;
    dst_node = tp_find_sw(outsw);
    outport = port_end;

    do{
        if(dst_node != NULL) // store in local
        {
            //流表下发
            memset(&fl, 0, sizeof(fl));
            memset(&mdata, 0, sizeof(mdata));
            of_mask_set_dc_all(&mask);
        
            fl.ip.nw_src = ip_src;
            of_mask_set_nw_src(&mask, 32);
            fl.ip.nw_dst = ip_dst;
            of_mask_set_nw_dst(&mask, 32);

            mul_app_act_alloc(&mdata);
            mul_app_act_set_ctors(&mdata, dst_node->sw_dpid);
            mul_app_action_output(&mdata, outport); 

            mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dst_node->sw_dpid, &fl, &mask,
                                (uint32_t)-1, mdata.act_base, mul_app_act_len(&mdata),
                                0, 0, C_FL_PRIO_FWD, C_FL_ENT_NOCACHE);
        }

        if(find_node(outsw)->value->rt_pre != 0)
        {
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

            outsw = find_node(find_node(outsw)->value->rt_pre)->value->node_id;
            dst_node = tp_find_sw(outsw);
        }
        else
            break;
        
    }while(1);
    mul_app_act_free(&mdata);

    // free topo (hash map & node)
    del_all_node();
    node_sw_num = 0;
    return 1;
}