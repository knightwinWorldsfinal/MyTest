#include<stdio.h>
#include<stdlib.h>
#include"server.h"


//main函数需要简洁
int main(int argc, char* argv[]) {
    //a.out port path(服务器提供的资源目录) 
    //argv[0]=a.out  argv[1] = port
    if (argc < 3) {
        printf("./a.out port respath\n"); 
        exit(0);
    }
    //资源根目录存储到argv[2]中，假设/home/pami/zjm
    //将当前服务器的进程工作目录切换到资源根目录中
    
    chdir(argv[2]);
    //启动服务器->基于epoll
    unsigned short port = atoi(argv[1]);
    epollRun(port);
    return 0;
}

//int main() {
//    //a.out port path(服务器提供的资源目录) 
//    //argv[0]=a.out  argv[1] = port
//    //if (argc < 3) {
//    //    printf("./a.out port respath\n");
//    //    exit(0);
//    //}
//    //资源根目录存储到argv[2]中，假设/home/pami/zjm
//    //将当前服务器的进程工作目录切换到资源根目录中
//    
//
//    chdir("/home/pami/zjm/http_source");
//    //启动服务器->基于epoll
//    unsigned short port = atoi("9999");
//    epollRun(port);
//    return 0;
//}