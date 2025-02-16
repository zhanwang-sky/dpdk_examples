//
//  main.c
//  dpdk_examples
//
//  Created by zhanwang-sky on 2025/2/13.
//

#include <stdio.h>
#include <stdlib.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#define MSG_POOL_NAME "MSG_POOL"
#define MSG_POOL_SIZE 1024
#define MSG_SIZE 256
#define CACHE_SIZE 32

#define RING_0_NAME "RING_0"
#define RING_1_NAME "RING_1"
#define RING_SIZE 128

struct rte_mempool* msg_pool;
struct rte_ring* recv_ring;
struct rte_ring* send_ring;

static __rte_noreturn void
app_main_loop(void* arg) {
    enum rte_proc_type_t proc_type = *((enum rte_proc_type_t*) arg);
    char* msg;

    RTE_LOG(INFO, USER1, "Running main loop on lcore %u\n", rte_lcore_id());

    if (proc_type == RTE_PROC_SECONDARY) {
        while (rte_mempool_get(msg_pool, (void**) &msg) < 0) {
            RTE_LOG(WARNING, USER1, "Could not get msg, wait for a second\n");
            rte_delay_us_sleep(1000 * 1000);
            continue;
        }
        snprintf(msg, MSG_SIZE, "hello from SECONDARY, msg_ptr=%p", msg);
        while (rte_ring_enqueue(send_ring, msg) < 0) {
            RTE_LOG(WARNING, USER1, "Could not send msg, wait for a second\n");
            rte_delay_us_sleep(1000 * 1000);
            continue;
        }
    }

    for (;;) {
        char* new_msg = NULL;

        // receive message
        while (rte_ring_dequeue(recv_ring, (void**) &msg) < 0) {
            rte_delay_us_sleep(1);
        }
        RTE_LOG(INFO, USER1, "Received message from peer: `%s` (%p)\n", msg, msg);

        // send new message
        do {
            if (rte_mempool_get(msg_pool, (void**) &new_msg) < 0) {
                RTE_LOG(WARNING, USER1, "Could not get new msg\n");
                break;
            }
            snprintf(new_msg, MSG_SIZE, "echo from %s, msg_ptr=%p",
                     (proc_type == RTE_PROC_PRIMARY ? "PRIMARY" : "SECONDARY"),
                     new_msg);
            if (rte_ring_enqueue(send_ring, new_msg) < 0) {
                RTE_LOG(WARNING, USER1, "Could not send new msg\n");
                rte_mempool_put(msg_pool, new_msg);
                new_msg = NULL;
                break;
            }
        } while (0);

        rte_mempool_put(msg_pool, msg);

        if (new_msg) {
            rte_delay_us_sleep(1000 * 1000);
        }
    }
}

int main(int argc, char* argv[]) {
    int rc;
    enum rte_proc_type_t proc_type;

    rc = rte_eal_init(argc, argv);
    if (rc < 0) {
        rte_exit(EXIT_FAILURE, "Fail to init EAL, %s\n", rte_strerror(rte_errno));
    }

    proc_type = rte_eal_process_type();
    RTE_LOG(INFO, USER1, "proc_type=%d\n", proc_type);
    RTE_LOG(INFO, USER1, "lcore_count=%u\n", rte_lcore_count());

    RTE_LOG(NOTICE, USER1, "&proc_type=%p, &app_main_loop=%p\n",
            &proc_type, &app_main_loop);

    if (proc_type == RTE_PROC_PRIMARY) {
        msg_pool = rte_mempool_create(MSG_POOL_NAME,
                                      MSG_POOL_SIZE, MSG_SIZE, CACHE_SIZE, 0,
                                      NULL, NULL, NULL, NULL,
                                      rte_socket_id(), 0);
        recv_ring = rte_ring_create(RING_0_NAME, RING_SIZE,
                                    rte_socket_id(), 0);
        send_ring = rte_ring_create(RING_1_NAME, RING_SIZE,
                                    rte_socket_id(), 0);
    } else {
        msg_pool = rte_mempool_lookup(MSG_POOL_NAME);
        recv_ring = rte_ring_lookup(RING_1_NAME);
        send_ring = rte_ring_lookup(RING_0_NAME);
    }

    if (!msg_pool || !recv_ring || !send_ring) {
        rte_exit(EXIT_FAILURE, "Could not create/lookup shared variables\n");
    }

    RTE_LOG(NOTICE, USER1, "&msg_pool=%p, msg_pool=%p\n", &msg_pool, msg_pool);
    RTE_LOG(NOTICE, USER1, "&recv_ring=%p, recv_ring=%p\n", &recv_ring, recv_ring);
    RTE_LOG(NOTICE, USER1, "&send_ring=%p, send_ring=%p\n", &send_ring, send_ring);

    app_main_loop(&proc_type);

    rc = rte_eal_cleanup();
    if (rc < 0) {
        rte_exit(EXIT_FAILURE, "Fail to cleanup EAL, %s\n", rte_strerror(rte_errno));
    }

    return 0;
}
