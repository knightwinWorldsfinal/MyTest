#pragma once
#ifndef SERVER_H
#define SERVER_H
//服务器要处理的业务逻辑
//初始监听的文件描述符

int initListenFd(unsigned short port);
int epollRun(unsigned short port);
//与客户端建立新链接
int acceptConn(int lfd, int epfd);
//接收客户端的http消息
int recvHttpRequest(int cfd, int epfd);
//解析请求行
int parseRequestLine(int cfd, const char* reqLine);

//发送头信息（状态行+消息报头+空行）
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);
//发送文件内容
int sendFile(int cfd, const char* fileName);
//发送目录
int sendDir(int cfd, const char* dirName);

//和客户端断连接
int disConnect(int cfd, int epfd);

//通过文件后缀得到文件的content-Type
const char *getFileType(const char *name);

//解码
int hexit(char c);
void decodeMsg(char* to, char* from);

#endif