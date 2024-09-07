#include"server.h"
#include<arpa/inet.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<errno.h>
#include<stdio.h>
#include<sys/stat.h>
#include<strings.h>
#include<string.h>
#include<dirent.h>
#include<unistd.h>


int initListenFd(unsigned short port)
{
    //1.创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        return -1;
    }

    //2.设置端口复用
    int opt = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR,&opt,sizeof(opt));
    if (ret == -1) {
        perror("setsockopt");
        return -1;
    }
    //3.绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        perror("bind");
        return -1;
    }
    //4.设置监听
    ret = listen(lfd, 128);
    if (ret == -1) {
        perror("listen");
        return -1;
    }
    //5.将得到的可用套接字返回给调用者
    return lfd;
}


int epollRun(unsigned short port)
{
    //1.创建epoll模型
    int epfd = epoll_create(10);
    if (epfd == -1) {
        perror("epoll_create");
        return -1;
    }
    //2.初始化epoll模型
    int lfd = initListenFd(port);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    //将epoll文件描述符挂载到模型上（红黑树）
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if (ret == -1) {
        perror("epoll_ctl");
        return -1;
    }

    //检测-循环检测
    struct epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);
    int flag = 0;
    while (1) {
        if (flag)//flag为1则出错中止循环
        {
            break;
        }

        //主线程不停的（监听）epoll_wait()，其余留给子线程处理
        int num = epoll_wait(epfd, evs, size, -1);
        for (int i = 0; i < num; ++i) {
            int curfd = evs[i].data.fd;
            if (curfd == lfd) {
                //此时文件描述符为监听描述符则建立新链接
                //创建子线程，在子线程中建立新的链接
                int ret = acceptConn(lfd, epfd);
                if (ret == -1)
                {
                    int flag = 1;
                    break;
                }


            }
            else {
                //否则为通信->先接收数据，再回复 
                //创建子线程，在子线程中建立新的链接
                recvHttpRequest(curfd, epfd);
                
            }
        }

    }
    return 0;
}


int acceptConn(int lfd, int epfd)
{
    //1.建立新链接
    int cfd = accept(lfd, NULL, NULL);
    if (cfd == -1)
    {
        perror("accept");
        return -1;
    }
    //2.设置非阻塞
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    //3.通信文件描述符添加到epoll模型中
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;//边沿模式
    ev.data.fd = cfd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if (ret == -1) {
        perror("epoll_ctl");
        return -1;
    }
    return 0;
}

int recvHttpRequest(int cfd, int epfd)
{
    //边沿非阻塞模式，所以循环读数据
    char tmp[1024];
    char buf[4096];
    //循环读数据
    int len, total = 0;//total:当前buff中已经存了多少数据
    //没必要将所有的http请求保存下来
    //需要的数据在请求行中
    //客户端向服务器请求都是静态资源，请求的资源内容在请求行的第二部分
    //只需要将请求完整的保存下来就可以，请求行后边请求头和空行
    //不需要解析请求头中的数据，因此接收到 之后不存储也是没问题的
    while ((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0){
        if (total + len < sizeof(buf)) {
            //还有空间存储,从当前位置开始继续存
            memcpy(buf + total, tmp, len);
        }
        total += len;
    }

    //循环结束
    //判断读操作是非阻塞的，当前缓存中没有数据值返回-1，errno=EAGAIN
    if (len == -1 && errno == EAGAIN) {
        //将请求行从接收数据中拿出来
        //在HTTP协议中，换行使用的是\r\n
        //遍历字符串，当遇到第一个是\r\n的时候一位置请求行就拿到了
        char* pt = strstr(buf, "\r\n");
        //请求行字节数（长度）
        int reqlen = pt - buf;//高地址-低地址，得到请求行长度
        //保留请求行
        buf[reqlen] = '\0';
        //解析请求行
        parseRequestLine(cfd, buf);

    }
    else if (len == 0) {
        //客户端断开连接,文件描述符从epoll中删除
        printf("客户端断开连接");
        disConnect(cfd, epfd);
    }
    else {
        perror("recv");
        return -1;
    }
    return 0;
}



int parseRequestLine(int cfd, const char* reqLine) {
    //请求行分为三部分
    //GET /资源文件路径/ http/1.1
    //1.将请求行三部分拆分，有用的为前两部分
    // -提交数据的方式
    // -客户端向服务器请求的文件名
    char method[6];
    char path[1024];
    sscanf(reqLine, "%[^ ] %[^ ]", method, path);
    //2.判断请求方式是不是get，不是get直接忽略
    //http中不区分大小写 get GET
    if (strcasecmp(method, "get") != 0) {
        printf("用户提交的不是get请求");
        return -1;

    }

    //3.判断用户提交的请求是要访问服务器的文件还是目录
    // 第一个/：服务器提供的资源根目录，在服务器端可以随意指定
    // 判断得到的文件属性 -stat()
    //判断path中存储的到底是什么字符串
    char* file = NULL;
    //如果文件名中有中文，需要还原
    decodeMsg(path, path);
    if (strcmp(path, "/") == 0) {
        //访问的是服务器提供的资源根目录,假设是/home/pami/zjm
        //如何在服务器端将服务器的资源根目录描述出来？
        //在启动服务器程序的时候，先指定资源根目录是那个目录chdir()
        file = "./";//对应的目录就是客户端访问的资源根目录

    }
    else {
        //file中保存的就是相对路径   
        file = path + 1;

    }



    //属性判断
    struct stat st;
    int ret = stat(file, &st);
    if (ret == -1) {
        //获取文件属性失败->没有这个文件
        //给客户端发送404页面
        sendHeadMsg(cfd, 404, "Not Found",getFileType(".html"), -1);
        sendFile(cfd,"404.html");
    }
    if (S_ISDIR(st.st_mode)) {
        //遍历目录，将目录的内容发送给客户端
        //4.客户端请求的名字是一个目录，遍历目录，发送目录内容给客户端
        sendHeadMsg(cfd,200,"OK",getFileType(".html"),-1);
        sendDir(cfd, file);
    }
    else {
        //5.客户端请求的名字是一个文件，发送文件内容给客户端
        //如果是普通文件，发送文件内容给客户端
        sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
        sendFile(cfd,file);

    }



    return 0;
}



//status：状态码
//descr：状态描述
//type :content-type
//length:content-length
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length)
{
    //状态行+消息报头+空行
    char buf[4096];
    //http/1.1 200 ok
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
    //消息报头  2个键值对
    //content-type   
    sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
    //content-length+ //空行
    sprintf(buf + strlen(buf), "Content-Length: %d\r\n\r\n", length);
    //拼接完成后发送
    send(cfd, buf, strlen(buf), 0);
    return 0;
}

int sendFile(int cfd, const char* fileName)
{
    //发送内容前，应该有状态行+消息报头+空行+文件内容
    //读文件内容，发送给客户端
    //打开文件
    int fd = open(fileName, O_RDONLY);
    //循环读文件
    while (1) {
        char buf[1024] = { 0 };
        int len = read(fd, buf, sizeof(buf));
        if (len > 0) {
            //发送读出的文件内容
            send(cfd, buf, len, 0);
            //！！！发送端发送太快，会导致浏览器解析不过来，导致很多bug，图片显示不全等！！！
            usleep(50);
        }
        else if (len == 0)
        {
            //文件读完了
            break;
        }
        else {
            perror("读文件失败");
            return -1;
        }

    }


    return 0;
}

//客户端访问目录，服务器需要遍历当前目录，并且将目录的所有文件名发送给客户端即可
//客户端访问目录，服务器需要遍历当前目录，并且将目录中的所有文件名发送给客户端即可-遍历目录得到的文件名需要放到htm1的表格中回复的数据是htm1格式的数据块
//<html>
//    <head>
//        <title>test</title>
//    </head>
//    <body>
//        <table>
//        <tr>
//            <td>文件名</td>
//            <td>文件大小</td>
//        </tr>
//        </table>
//    </body>
//<html>

int sendDir(int cfd, const char* dirName)
{
    struct dirent** namelist;
    char buf[4096];
    sprintf(buf, "<html><head><title>%s</title><head><body><table>", dirName);
    int num = scandir(dirName,&namelist,NULL,alphasort);
    //遍历各个文件，将其文件名和大小以html表格形式发送，最后补充html结尾
    for (int i = 0; i < num;++i) {
        //取出文件名
        char* name = namelist[i]->d_name;
        //拼接当前文件在资源文件中的相对路径
        char subpath[1024];
        sprintf(subpath,"% s/% s" , dirName, name);
        //stat函数的第一个参数文件路径
        struct stat st;
        stat(subpath, &st);
        if (S_ISDIR(st.st_mode)) {
            //如果是目录，超链接的跳转路径文件名后边加/
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", //第一个%s是告诉超链接跳转到哪去，第二个是文件名
                name, name, (long)st.st_size);
        }
        else {
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                name, name, (long)st.st_size);
        }
        //
        

        //发送数据
        send(cfd, buf, strlen(buf),0);
        memset(buf, 0, sizeof(buf));
        //释放资源 namelist[i]这个指针
        free(namelist[i]);
    }
    //补充html中结尾剩余的标签
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    //释放namelist
    free(namelist);

    return 0;
}

int disConnect(int cfd, int epfd)
{
    //将cdf从epoll模型上删除
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret == -1) {
        perror("epoll_ctl");
        return -1;
    }
    close(cfd);
    return 0;
}

//通过文件名获取文件类型
//参数name->文件名
//返回值这个文件对于的content-Type的类型
const char *getFileType(const char *name) {
    const char* dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";//纯文本
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset-utf-8";

}

//16进制转10进制整数
int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}


//from：要被转换的->传入参数
//to:转换之后得到的字符->传出参数
void decodeMsg(char* to, char* from) {
    for (; *from != '\0'; ++to, ++from) {
        //isxdigit ->判断字符是不是16进制格式
        //Linux %E5%86%85%A0%B8.jpg 示例16进制乱码
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
            //将16进制数->转化为十进制  将这个数值幅值给了字符int ->char
            //A1 ==161
            *to = hexit(from[1]) * 16 + hexit(from[2]);
            from += 2;//from中+1了，再+2 隔3个一组16进制数
        }
        else {
            //不是特殊字符字节赋值
            *to = *from;
        }
    }
    *to = '\0';
}
