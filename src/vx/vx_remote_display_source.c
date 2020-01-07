#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ifaddrs.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "vx_remote_display_source.h"
#include "vx_code_output_stream.h"
#include "vx_tcp_display.h"

#include "common/ssocket.h"
#include "common/ioutils.h"

struct vx_remote_display_source
{
    vx_application_t app;

    int shutdown;

    vx_remote_display_source_attr_t attr;

    // advertise thread
    //uint16_t ad_port;
    int ad_sock;// udp send socket
    pthread_t ad_thread;

    // accept connection thread
    //int accept_port;
    ssocket_t * accept_socket;
    pthread_t accept_thread;

    // Store data about currently active displays
    zarray_t * disp_list;
    pthread_mutex_t store_mutex; // ensure that access to the list is properly synchronized
    pthread_cond_t store_cond; // signal each time something is removed from the list, so we can exit cleanly
};

static int verbose = 0;

void vx_remote_display_source_attr_init(vx_remote_display_source_attr_t * attr)
{
    attr->advertise_port = DEFAULT_VX_AD_PORT;
    attr->connection_port = DEFAULT_VX_CXN_PORT;
    // make sure free() will work
    char * temp =  "Vx Application";
    attr->advertise_name = strdup(temp);
    attr->max_bandwidth_KBs = -1;
}

void vx_remote_display_source_attr_destroy(vx_remote_display_source_attr_t * attr)
{
    free(attr->advertise_name);
    free(attr);
}

static void display_closed_callback(vx_display_t * disp, void * priv)
{
    if (verbose) printf("Attempting to destroy %p\n", (void*)disp);
    vx_remote_display_source_t * cxn_mgr = priv;

    // tell the user they need to do all cleanup related to this program closing
    cxn_mgr->app.display_finished(&cxn_mgr->app, disp);

    // do our cleanup:
    vx_tcp_display_destroy(disp);

    pthread_mutex_lock(&cxn_mgr->store_mutex);
    zarray_remove_value(cxn_mgr->disp_list, &disp, 0);

    pthread_cond_signal(&cxn_mgr->store_cond);
    if (verbose) printf("Removed %p new # of displays is %d\n", (void*)disp, zarray_size(cxn_mgr->disp_list));

    pthread_mutex_unlock(&cxn_mgr->store_mutex);
}



static void * accept_run(void * vptr)
{
    vx_remote_display_source_t * cxn_mgr = vptr;
    if (verbose) printf("Starting accept thread on %d\n",cxn_mgr->attr.connection_port);

    if (ssocket_listen(cxn_mgr->accept_socket, cxn_mgr->attr.connection_port, 1, 0) < 0) {
        perror("Failed to listen to accept socket! Exiting.\n");
        exit(1);
    }

    while(1) {
        ssocket_t * cxn = ssocket_accept(cxn_mgr->accept_socket);
        if (cxn == NULL) {
            printf("vx_remote_display_source: CXN  is null!!!\n");
            continue;
            /* break; */ // XXXX When we are also starting the GL thread simultaneously, we get an interrupt here. Not sure why
        }

        vx_display_t * disp = vx_tcp_display_create(cxn, cxn_mgr->attr.max_bandwidth_KBs,
                                                    display_closed_callback, cxn_mgr);
        cxn_mgr->app.display_started(&cxn_mgr->app, disp);

        pthread_mutex_lock(&cxn_mgr->store_mutex);
        zarray_add(cxn_mgr->disp_list, &disp);
        pthread_mutex_unlock(&cxn_mgr->store_mutex);

    }
    if (verbose) printf("Accept thread is exiting\n");

    pthread_exit(NULL);
}

/** make and bind a udp socket to an ephemeral port. Returns the fd. **/
static int udp_socket_create(void)
{
    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return -1;

    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(struct sockaddr_in));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = INADDR_ANY; // ephemeral port, please
    listen_addr.sin_addr.s_addr = INADDR_ANY;

    int broadcastEnable=1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0)
        perror("setsockopt bcast");

    int res = bind(sock, (struct sockaddr*) &listen_addr, sizeof(struct sockaddr_in));
    if (res < 0)
        return -2;

    return sock;
}

static void * ad_run(void * vptr)
{
    if (verbose) printf("Starting advertisement thread\n");
    vx_remote_display_source_t * cxn_mgr = vptr;

    if (cxn_mgr->attr.advertise_port == 0) {
        printf("NFO: Not starting remote advertise thread\n");
        return NULL;
    }

    // store the sockaddr_in structs by value
    zarray_t * bcast_addrs = zarray_create(sizeof(struct sockaddr_in));

    struct ifaddrs *head = NULL;
    if (getifaddrs(&head) != 0) {
        perror("Failed to read interfaces");
        exit(1);
    }

    for (struct ifaddrs * cur = head; cur != NULL; cur = cur->ifa_next) {
        if (cur->ifa_addr == NULL) {
            printf("No address here!\n");
            continue;
        }

        if (cur->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in remote_addr;
            memset(&remote_addr, 0, sizeof(struct sockaddr_in));
            remote_addr.sin_family = AF_INET;
            remote_addr.sin_port = htons(cxn_mgr->attr.advertise_port);

            memcpy(&remote_addr.sin_addr, &((struct sockaddr_in*)cur->ifa_ifu.ifu_broadaddr)->sin_addr, sizeof(struct in_addr));

            // For future reference: (bcast only valid for IPv4)
            /* char * addr = inet_ntoa(((struct sockaddr_in*)cur->ifa_addr)->sin_addr); */
            /* char * bcast = inet_ntoa(((struct sockaddr_in*)cur->ifa_ifu.ifu_broadaddr)->sin_addr); */
            /* printf("iface %s: addr %s  bcast %s\n", cur->ifa_name, addr, bcast); */

            zarray_add(bcast_addrs, &remote_addr);
        }
    }

    freeifaddrs(head);

    vx_code_output_stream_t * outs = vx_code_output_stream_create(1024);
    outs->write_uint32(outs, VX_AD_MAGIC);
    outs->write_uint32(outs, cxn_mgr->attr.connection_port);
    outs->write_str(outs, cxn_mgr->attr.advertise_name);

    while (1) {
        for (int i = 0; i < zarray_size(bcast_addrs); i++) {
            struct sockaddr_in * bcast = NULL;
            zarray_get_volatile(bcast_addrs, i, &bcast);

            if (sendto(cxn_mgr->ad_sock, outs->data, outs->pos, 0,
                       (struct sockaddr*) bcast, sizeof(struct sockaddr_in)) < 0) {

                // errno is usually EBADF on shutdown
                if (errno != EBADF || !cxn_mgr->shutdown)
                    perror("sendto");

                // ignore errors (except printing them)
            }
        }
        if (cxn_mgr->shutdown)
            break;
        sleep(1);
    }

    zarray_destroy(bcast_addrs);

    if (verbose) printf("Advertisement thread is exiting\n");
    vx_code_output_stream_destroy(outs);

    pthread_exit(NULL);
}



vx_remote_display_source_t * vx_remote_display_source_create_attr(vx_application_t * app,
                                                                  const vx_remote_display_source_attr_t * attr)
{
    vx_remote_display_source_t * cxn_mgr = calloc(1, sizeof(vx_remote_display_source_t));
    memcpy(&cxn_mgr->app, app, sizeof(vx_application_t));

    if (attr != NULL) {
        memcpy(&cxn_mgr->attr, attr, sizeof(vx_remote_display_source_attr_t));
        cxn_mgr->attr.advertise_name = strdup(attr->advertise_name);
    } else {
        // set defaults
        vx_remote_display_source_attr_init(&cxn_mgr->attr);
    }

    cxn_mgr->disp_list = zarray_create(sizeof(vx_display_t*));

    pthread_mutex_init(&cxn_mgr->store_mutex, NULL);
    pthread_cond_init(&cxn_mgr->store_cond, NULL);

    cxn_mgr->accept_socket = ssocket_create();
    cxn_mgr->ad_sock  = udp_socket_create();
    if (cxn_mgr->ad_sock < 0) {
        printf("Failed to get socket %d\n", cxn_mgr->ad_sock);
        exit(1);
    }


    pthread_create(&cxn_mgr->accept_thread, NULL, accept_run, cxn_mgr);

    pthread_create(&cxn_mgr->ad_thread, NULL, ad_run, cxn_mgr);


    return cxn_mgr;
}

// Create a new connection manager, with a callback when a new connection is established
vx_remote_display_source_t * vx_remote_display_source_create(vx_application_t * app)
{
    return vx_remote_display_source_create_attr(app, NULL);
}

void vx_remote_display_source_destroy(vx_remote_display_source_t* cxn_mgr)
{
    cxn_mgr->shutdown = 1;

    // Start by exiting the advertise thread
    close(cxn_mgr->ad_sock);
    pthread_join(cxn_mgr->ad_thread, NULL);
    if (verbose) printf("Ad thread joined\n");

    // then close the thread which waits for new connections:
    // XXX/TODO The rest of this function hangs, and generally causes issues
    pthread_cancel(cxn_mgr->accept_thread); // causes small memory leak of pthread_t
    ssocket_destroy(cxn_mgr->accept_socket);
    pthread_join(cxn_mgr->accept_thread, NULL);
    if (verbose) printf("Accept thread joined\n");



    // close active each socket in vx_tcp_display.
    // XXX Slight chance that the socket goes down concurrently. Maybe need to expand lines where mutices are used?
    pthread_mutex_lock(&cxn_mgr->store_mutex);
    zarray_vmap(cxn_mgr->disp_list, vx_tcp_display_close);
    pthread_mutex_unlock(&cxn_mgr->store_mutex);
    if (verbose) printf("All displays are flagged for closure\n");
    printf("NFO: If program doesn't quit, please exit all remote viewers.\n"); // XXX This is still a bug

    // now need to wait until the list gets emptied
    while(1) {
        pthread_mutex_lock(&cxn_mgr->store_mutex);
        if (zarray_size(cxn_mgr->disp_list) == 0) {
            pthread_mutex_unlock(&cxn_mgr->store_mutex);
            break;
        }
        pthread_cond_wait(&cxn_mgr->store_cond, &cxn_mgr->store_mutex);
        pthread_mutex_unlock(&cxn_mgr->store_mutex);
    }


    pthread_mutex_destroy(&cxn_mgr->store_mutex);
    pthread_cond_destroy(&cxn_mgr->store_cond);
    zarray_destroy(cxn_mgr->disp_list);

    free(cxn_mgr);
}
