#include<stdio.h>
#include<stdlib.h>
#include"server.h"


//main������Ҫ���
int main(int argc, char* argv[]) {
    //a.out port path(�������ṩ����ԴĿ¼) 
    //argv[0]=a.out  argv[1] = port
    if (argc < 3) {
        printf("./a.out port respath\n"); 
        exit(0);
    }
    //��Դ��Ŀ¼�洢��argv[2]�У�����/home/pami/zjm
    //����ǰ�������Ľ��̹���Ŀ¼�л�����Դ��Ŀ¼��
    
    chdir(argv[2]);
    //����������->����epoll
    unsigned short port = atoi(argv[1]);
    epollRun(port);
    return 0;
}

//int main() {
//    //a.out port path(�������ṩ����ԴĿ¼) 
//    //argv[0]=a.out  argv[1] = port
//    //if (argc < 3) {
//    //    printf("./a.out port respath\n");
//    //    exit(0);
//    //}
//    //��Դ��Ŀ¼�洢��argv[2]�У�����/home/pami/zjm
//    //����ǰ�������Ľ��̹���Ŀ¼�л�����Դ��Ŀ¼��
//    
//
//    chdir("/home/pami/zjm/http_source");
//    //����������->����epoll
//    unsigned short port = atoi("9999");
//    epollRun(port);
//    return 0;
//}