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
    //1.�����������׽���
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        return -1;
    }

    //2.���ö˿ڸ���
    int opt = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR,&opt,sizeof(opt));
    if (ret == -1) {
        perror("setsockopt");
        return -1;
    }
    //3.��
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        perror("bind");
        return -1;
    }
    //4.���ü���
    ret = listen(lfd, 128);
    if (ret == -1) {
        perror("listen");
        return -1;
    }
    //5.���õ��Ŀ����׽��ַ��ظ�������
    return lfd;
}


int epollRun(unsigned short port)
{
    //1.����epollģ��
    int epfd = epoll_create(10);
    if (epfd == -1) {
        perror("epoll_create");
        return -1;
    }
    //2.��ʼ��epollģ��
    int lfd = initListenFd(port);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    //��epoll�ļ����������ص�ģ���ϣ��������
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if (ret == -1) {
        perror("epoll_ctl");
        return -1;
    }

    //���-ѭ�����
    struct epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);
    int flag = 0;
    while (1) {
        if (flag)//flagΪ1�������ֹѭ��
        {
            break;
        }

        //���̲߳�ͣ�ģ�������epoll_wait()�������������̴߳���
        int num = epoll_wait(epfd, evs, size, -1);
        for (int i = 0; i < num; ++i) {
            int curfd = evs[i].data.fd;
            if (curfd == lfd) {
                //��ʱ�ļ�������Ϊ��������������������
                //�������̣߳������߳��н����µ�����
                int ret = acceptConn(lfd, epfd);
                if (ret == -1)
                {
                    int flag = 1;
                    break;
                }


            }
            else {
                //����Ϊͨ��->�Ƚ������ݣ��ٻظ� 
                //�������̣߳������߳��н����µ�����
                recvHttpRequest(curfd, epfd);
                
            }
        }

    }
    return 0;
}


int acceptConn(int lfd, int epfd)
{
    //1.����������
    int cfd = accept(lfd, NULL, NULL);
    if (cfd == -1)
    {
        perror("accept");
        return -1;
    }
    //2.���÷�����
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    //3.ͨ���ļ���������ӵ�epollģ����
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;//����ģʽ
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
    //���ط�����ģʽ������ѭ��������
    char tmp[1024];
    char buf[4096];
    //ѭ��������
    int len, total = 0;//total:��ǰbuff���Ѿ����˶�������
    //û��Ҫ�����е�http���󱣴�����
    //��Ҫ����������������
    //�ͻ���������������Ǿ�̬��Դ���������Դ�����������еĵڶ�����
    //ֻ��Ҫ�����������ı��������Ϳ��ԣ������к������ͷ�Ϳ���
    //����Ҫ��������ͷ�е����ݣ���˽��յ� ֮�󲻴洢Ҳ��û�����
    while ((len = recv(cfd, tmp, sizeof(tmp), 0)) > 0){
        if (total + len < sizeof(buf)) {
            //���пռ�洢,�ӵ�ǰλ�ÿ�ʼ������
            memcpy(buf + total, tmp, len);
        }
        total += len;
    }

    //ѭ������
    //�ж϶������Ƿ������ģ���ǰ������û������ֵ����-1��errno=EAGAIN
    if (len == -1 && errno == EAGAIN) {
        //�������дӽ����������ó���
        //��HTTPЭ���У�����ʹ�õ���\r\n
        //�����ַ�������������һ����\r\n��ʱ��һλ�������о��õ���
        char* pt = strstr(buf, "\r\n");
        //�������ֽ��������ȣ�
        int reqlen = pt - buf;//�ߵ�ַ-�͵�ַ���õ������г���
        //����������
        buf[reqlen] = '\0';
        //����������
        parseRequestLine(cfd, buf);

    }
    else if (len == 0) {
        //�ͻ��˶Ͽ�����,�ļ���������epoll��ɾ��
        printf("�ͻ��˶Ͽ�����");
        disConnect(cfd, epfd);
    }
    else {
        perror("recv");
        return -1;
    }
    return 0;
}



int parseRequestLine(int cfd, const char* reqLine) {
    //�����з�Ϊ������
    //GET /��Դ�ļ�·��/ http/1.1
    //1.�������������ֲ�֣����õ�Ϊǰ������
    // -�ύ���ݵķ�ʽ
    // -�ͻ����������������ļ���
    char method[6];
    char path[1024];
    sscanf(reqLine, "%[^ ] %[^ ]", method, path);
    //2.�ж�����ʽ�ǲ���get������getֱ�Ӻ���
    //http�в����ִ�Сд get GET
    if (strcasecmp(method, "get") != 0) {
        printf("�û��ύ�Ĳ���get����");
        return -1;

    }

    //3.�ж��û��ύ��������Ҫ���ʷ��������ļ�����Ŀ¼
    // ��һ��/���������ṩ����Դ��Ŀ¼���ڷ������˿�������ָ��
    // �жϵõ����ļ����� -stat()
    //�ж�path�д洢�ĵ�����ʲô�ַ���
    char* file = NULL;
    //����ļ����������ģ���Ҫ��ԭ
    decodeMsg(path, path);
    if (strcmp(path, "/") == 0) {
        //���ʵ��Ƿ������ṩ����Դ��Ŀ¼,������/home/pami/zjm
        //����ڷ������˽�����������Դ��Ŀ¼����������
        //�����������������ʱ����ָ����Դ��Ŀ¼���Ǹ�Ŀ¼chdir()
        file = "./";//��Ӧ��Ŀ¼���ǿͻ��˷��ʵ���Դ��Ŀ¼

    }
    else {
        //file�б���ľ������·��   
        file = path + 1;

    }



    //�����ж�
    struct stat st;
    int ret = stat(file, &st);
    if (ret == -1) {
        //��ȡ�ļ�����ʧ��->û������ļ�
        //���ͻ��˷���404ҳ��
        sendHeadMsg(cfd, 404, "Not Found",getFileType(".html"), -1);
        sendFile(cfd,"404.html");
    }
    if (S_ISDIR(st.st_mode)) {
        //����Ŀ¼����Ŀ¼�����ݷ��͸��ͻ���
        //4.�ͻ��������������һ��Ŀ¼������Ŀ¼������Ŀ¼���ݸ��ͻ���
        sendHeadMsg(cfd,200,"OK",getFileType(".html"),-1);
        sendDir(cfd, file);
    }
    else {
        //5.�ͻ��������������һ���ļ��������ļ����ݸ��ͻ���
        //�������ͨ�ļ��������ļ����ݸ��ͻ���
        sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
        sendFile(cfd,file);

    }



    return 0;
}



//status��״̬��
//descr��״̬����
//type :content-type
//length:content-length
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length)
{
    //״̬��+��Ϣ��ͷ+����
    char buf[4096];
    //http/1.1 200 ok
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
    //��Ϣ��ͷ  2����ֵ��
    //content-type   
    sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
    //content-length+ //����
    sprintf(buf + strlen(buf), "Content-Length: %d\r\n\r\n", length);
    //ƴ����ɺ���
    send(cfd, buf, strlen(buf), 0);
    return 0;
}

int sendFile(int cfd, const char* fileName)
{
    //��������ǰ��Ӧ����״̬��+��Ϣ��ͷ+����+�ļ�����
    //���ļ����ݣ����͸��ͻ���
    //���ļ�
    int fd = open(fileName, O_RDONLY);
    //ѭ�����ļ�
    while (1) {
        char buf[1024] = { 0 };
        int len = read(fd, buf, sizeof(buf));
        if (len > 0) {
            //���Ͷ������ļ�����
            send(cfd, buf, len, 0);
            //���������Ͷ˷���̫�죬�ᵼ����������������������ºܶ�bug��ͼƬ��ʾ��ȫ�ȣ�����
            usleep(50);
        }
        else if (len == 0)
        {
            //�ļ�������
            break;
        }
        else {
            perror("���ļ�ʧ��");
            return -1;
        }

    }


    return 0;
}

//�ͻ��˷���Ŀ¼����������Ҫ������ǰĿ¼�����ҽ�Ŀ¼�������ļ������͸��ͻ��˼���
//�ͻ��˷���Ŀ¼����������Ҫ������ǰĿ¼�����ҽ�Ŀ¼�е������ļ������͸��ͻ��˼���-����Ŀ¼�õ����ļ�����Ҫ�ŵ�htm1�ı���лظ���������htm1��ʽ�����ݿ�
//<html>
//    <head>
//        <title>test</title>
//    </head>
//    <body>
//        <table>
//        <tr>
//            <td>�ļ���</td>
//            <td>�ļ���С</td>
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
    //���������ļ��������ļ����ʹ�С��html�����ʽ���ͣ���󲹳�html��β
    for (int i = 0; i < num;++i) {
        //ȡ���ļ���
        char* name = namelist[i]->d_name;
        //ƴ�ӵ�ǰ�ļ�����Դ�ļ��е����·��
        char subpath[1024];
        sprintf(subpath,"% s/% s" , dirName, name);
        //stat�����ĵ�һ�������ļ�·��
        struct stat st;
        stat(subpath, &st);
        if (S_ISDIR(st.st_mode)) {
            //�����Ŀ¼�������ӵ���ת·���ļ�����߼�/
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", //��һ��%s�Ǹ��߳�������ת����ȥ���ڶ������ļ���
                name, name, (long)st.st_size);
        }
        else {
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                name, name, (long)st.st_size);
        }
        //
        

        //��������
        send(cfd, buf, strlen(buf),0);
        memset(buf, 0, sizeof(buf));
        //�ͷ���Դ namelist[i]���ָ��
        free(namelist[i]);
    }
    //����html�н�βʣ��ı�ǩ
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    //�ͷ�namelist
    free(namelist);

    return 0;
}

int disConnect(int cfd, int epfd)
{
    //��cdf��epollģ����ɾ��
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret == -1) {
        perror("epoll_ctl");
        return -1;
    }
    close(cfd);
    return 0;
}

//ͨ���ļ�����ȡ�ļ�����
//����name->�ļ���
//����ֵ����ļ����ڵ�content-Type������
const char *getFileType(const char *name) {
    const char* dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";//���ı�
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

//16����ת10��������
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


//from��Ҫ��ת����->�������
//to:ת��֮��õ����ַ�->��������
void decodeMsg(char* to, char* from) {
    for (; *from != '\0'; ++to, ++from) {
        //isxdigit ->�ж��ַ��ǲ���16���Ƹ�ʽ
        //Linux %E5%86%85%A0%B8.jpg ʾ��16��������
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
            //��16������->ת��Ϊʮ����  �������ֵ��ֵ�����ַ�int ->char
            //A1 ==161
            *to = hexit(from[1]) * 16 + hexit(from[2]);
            from += 2;//from��+1�ˣ���+2 ��3��һ��16������
        }
        else {
            //���������ַ��ֽڸ�ֵ
            *to = *from;
        }
    }
    *to = '\0';
}
