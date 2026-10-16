#include<stdio.h>
#include<liburing.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>

#define QLEN 1024
#define BUFLEN 64*1024

int err(char*s){
    perror(s);
    return 1;
}

int main(){
    int server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0) return err("socket failed");

    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(9000);

    if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0) return err("bind failed");

    if(listen(server_fd,8)<0) return err("listen failed");

    struct io_uring ring;
    if(io_uring_queue_init(QLEN,&ring,0)<0) return err("uring init");

    char buffers[2*QLEN][1024];
    int fds[2*QLEN];
    int i=0;
    memset(buffers,0,sizeof(buffers));

    struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe,server_fd,NULL,NULL,0);
    io_uring_sqe_set_data(sqe,(void*)(intptr_t)QLEN);
    io_uring_submit(&ring);

    while(true){
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring,&cqe);
        if(!cqe) return err("wait failed");

        if(cqe->res<0){
            printf("%s\n",strerror(-cqe->res));
            continue;
        }

        if(cqe->user_data == QLEN){
            struct io_uring_sqe *sqeAccept=io_uring_get_sqe(&ring);
            io_uring_prep_accept(sqeAccept,server_fd,NULL,NULL,0);
            io_uring_sqe_set_data(sqeAccept,(void*)(intptr_t)QLEN);

            struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
            fds[i]=cqe->res;
            io_uring_prep_read(sqe,fds[i],buffers[i],1024,0);
            io_uring_sqe_set_data(sqe,(void*)(intptr_t)i);
            i=(i+1)%(2*QLEN);
        }else if(cqe->user_data < QLEN){
            struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
            io_uring_prep_write(sqe,fds[cqe->user_data],buffers[cqe->user_data],1024,0);
            io_uring_sqe_set_data(sqe,(void*)(intptr_t)(QLEN+1));
        }else{
            struct io_uring_sqe *sqe=io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe,fds[sqe->user_data],buffers[sqe->user_data],1024,0);
            io_uring_sqe_set_data(sqe,(void*)sqe->user_data);
        }
        io_uring_cqe_seen(&ring,cqe);
        io_uring_submit(&ring);
    }
    io_uring_queue_exit(&ring);
    return 0;
}
