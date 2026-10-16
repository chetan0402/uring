#include<stdio.h>
#include<liburing.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<stdlib.h>

#define QLEN 1024
#define BUFLEN 64*1024

typedef enum {
    ACCEPT,
    READ,
    WRITE,
    CLOSE
} requesttype;

typedef struct{
    requesttype type;
    int client_fd;
} request;

struct io_uring ring;
int server_fd;
char ignore[1024];

void add_accept_to_ring(){
    request*r=malloc(sizeof(request));
    r->type=ACCEPT;
    struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe,server_fd,NULL,NULL,0);
    io_uring_sqe_set_data(sqe,r);
}

void add_close_to_ring(request*r){
    struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
    io_uring_prep_close(sqe,r->client_fd);
    r->type=CLOSE;
    io_uring_sqe_set_data(sqe,r);
}

void add_write_to_ring(request*r){
    struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
    io_uring_prep_write(sqe,r->client_fd,"pong\0",5,0);
    r->type=READ;
    io_uring_sqe_set_data(sqe,r);
}

void add_read_to_ring(request*r){
    struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe,r->client_fd,ignore,1024,0);
    r->type=READ;
    io_uring_sqe_set_data(sqe,r);
}

int err(char*s){
    perror(s);
    return 1;
}

int main(){
    server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0) return err("socket failed");

    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(9000);

    if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0) return err("bind failed");
    if(listen(server_fd,8)<0) return err("listen failed");
    if(io_uring_queue_init(QLEN,&ring,0)<0) return err("uring init");

    add_accept_to_ring();
    io_uring_submit(&ring);

    while(true){
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring,&cqe);
        if(!cqe) return err("wait failed");

        request*r=(request*)cqe->user_data;

        if(cqe->res<=0){
            if(cqe->res<0) printf("%s\n",strerror(-cqe->res));
            switch(r->type){
                case ACCEPT:
                    add_accept_to_ring();
                    break;
                case READ:
                case WRITE:
                    add_close_to_ring(r);
                    break;
                case CLOSE:
                    free(r);
            }
        }else{
            switch(r->type){
                case ACCEPT:
                    add_accept_to_ring();
                    r->client_fd=cqe->res;
                    add_write_to_ring(r);
                    break;
                case WRITE:
                case READ:
                    add_close_to_ring(r);
            }
        }
        io_uring_cqe_seen(&ring,cqe);
        io_uring_submit(&ring);
    }
    io_uring_queue_exit(&ring);
    return 0;
}
