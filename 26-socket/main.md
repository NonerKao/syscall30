### 前情提要

來到了最後一個篇章：網路篇！這個篇章裡面筆者打算介紹7個系統呼叫，因為這是組成一組TCP連線所需的系統呼叫的最小數目。

---
### 範例程式：伺服器端

伺服器端的程式碼主要分成以下幾個階段：

1. 生成socket，取得該socket的檔案描述子（SOCK_STREAM與IP_PROTO_IP的組合就是TCP/IP）
2. 將代表該socket的檔案描述子與一個網路位址結構連結起來
3. 聆聽該socket
4. 等待即將來臨的請求，如有請求出現則接受之，取得這個連線的檔案描述子
5. 寄送訊息給予步驟4中的檔案描述子
6. 關閉步驟4的檔案描述子（結束連線），回到步驟4

```
 19 int main(int argc, char *argv[]){
 20 
 21         int listenfd = 0, connfd = 0;
 22         struct sockaddr_in serv_addr;
 23         if(argc < 2){
 24                 printf("./server <port>\n");
 25                 exit(0);
 26         }
 27         char send_buf[1025];
 28         time_t ticks;
 29         int ret;
 30 
 31         SYSCALL_ERROR(listenfd, socket, AF_INET, SOCK_STREAM, IP_PROTO_IP);
 32 
 33         memset(&serv_addr, '0', sizeof(serv_addr));
 34         memset(send_buf, '0', sizeof(send_buf));
 35 
 36         serv_addr.sin_family = AF_INET;
 37         serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 38         serv_addr.sin_port = htons(atoi(argv[1]));
 39 
 40         SYSCALL_ERROR(ret, bind, listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
 41 
 42         SYSCALL_ERROR(ret, listen, listenfd, 0);
 43 
 44         while(1)
 45         {
 46                 struct sockaddr_in cli;
 47                 int clilen;
 48                 char ipaddr[20];
 49 
 50                 SYSCALL_ERROR(connfd, accept, listenfd, (struct sockaddr*)&cli, &clilen);
 51 
 52                 inet_ntop(AF_INET, &cli.sin_addr, ipaddr, sizeof(struct sockaddr));
 53                 printf("from IP: %s, port: %d\n", ipaddr, ntohs(cli.sin_port));
 54 
 55                 ticks = time(NULL);
 56                 sprintf(send_buf, "%.24s\n", ctime(&ticks));
 57 
 58                 SYSCALL_ERROR(ret, sendto, connfd, send_buf, strlen(send_buf), 0, NULL, 0);
 59 
 60                 sleep(1);
 61                 close(connfd);
 62         }
 63 }
```

---
### 範例程式：客戶端

客戶端的程式碼主要分成以下幾個階段：

1. 生成socket，取得該socket的檔案描述子
2. 生成伺服器的網路位址結構
3. 使用步驟1中的socket連接伺服器
4. 自該socket的檔案描述子讀取訊息

```
 18 int main(int argc, char *argv[]){
 19 
 20         int sockfd = 0;
 21         struct sockaddr_in serv_addr; 
 22         if(argc < 2){
 23                 printf("%s <ip> <port>\n", argv[0]);
 24                 exit(0);
 25         }
 26         char recv_buf[1025];
 27         int ret;
 28         int len;
 29 
 30         SYSCALL_ERROR(sockfd, socket, AF_INET, SOCK_STREAM, IPPROTO_IP);
 31 
 32         memset(&serv_addr, '0', sizeof(serv_addr));
 33         memset(recv_buf, '0', sizeof(recv_buf));
 34 
 35         serv_addr.sin_family = AF_INET;
 36         serv_addr.sin_port = htons(atoi(argv[2]));
 37 
 38         if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<0){
 39                 printf("\n inet_pton error occured\n");
 40                 return 1;
 41         }
 42 
 43         SYSCALL_ERROR(ret, connect, sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
 44 
 45         SYSCALL_ERROR(len, recvfrom, sockfd, recv_buf, strlen(recv_buf), 0, NULL, 0);
 46 
 47         recv_buf[len] = '\0';
 48         printf("message from server: %s", recv_buf);
 49 
 50         close(sockfd);
 51 
 52         return 0;
 53 }
```

> 為了避免不熟悉本系列的讀者覺得這些附帶行號的程式碼不好用，其實本系列文章以及範例程式碼都附在筆者的[github](https://github.com/NonerKao/syscall30)之中喔！

---
### 範例程式：執行狀況

以下是使用客戶端連線三次的可能的情況。伺服器端：
```
[demo@linux tcp]$ ./srv 1245 
from IP: 127.0.0.1, port: 40672
from IP: 127.0.0.1, port: 40714
from IP: 127.0.0.1, port: 40720
...(程式未終結)
```
客戶端：
```
[demo@linux tcp]$ ./cli 127.0.0.1 1245
message from server: Sun Dec 25 20:44:02 2016
[demo@linux tcp]$ ./cli 127.0.0.1 1245
message from server: Sun Dec 25 20:44:12 2016
[demo@linux tcp]$ ./cli 127.0.0.1 1245
message from server: Sun Dec 25 20:44:13 2016
[demo@linux tcp]$ 
```

如果是使用`strace`觀察的情況，伺服器端是：
```
[demo@linux tcp]$ strace ./srv 1247
...
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
bind(3, {sa_family=AF_INET, sin_port=htons(1247), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 0)                            = 0
accept(3, {sa_family=AF_INET, sin_port=htons(36298), sin_addr=inet_addr("127.0.0.1")}, [32519->16]) = 4
...
sendto(4, "Sun Dec 25 20:46:02 2016\n", 25, 0, NULL, 0) = 25
nanosleep({1, 0}, 0x7fff975f6f40)       = 0
close(4)                                = 0
accept(3,
...
```
筆者讓伺服器端程式每一次連線都送一個訊息就斷開，但是斷開之前會等待一秒的時間，因此這裡有一個`nanosleep`，不只如此，因為
所送的訊息是時間的tick，所以也會有其他系統呼叫參與讀取`/etc/localtime`的過程。客戶端的部份則是：
```
[demo@linux tcp]$ strace ./cli 127.0.0.1 1247
execve("./cli", ["./cli", "127.0.0.1", "1247"], [/* 40 vars */]) = 0
...
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
connect(3, {sa_family=AF_INET, sin_port=htons(1247), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
recvfrom(3, "Sun Dec 25 20:46:38 2016\n", 1025, 0, NULL, NULL) = 25
...
```
本日我們要順著這個執行的流程，先來觀察`socket`系統呼叫如何取得檔案描述子。

---
### 追蹤

`socket`位在`net/socket.c`之中，
```
1206 SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
1207 {
1208         int retval;
1209         struct socket *sock;
1210         int flags;
1211 
1212         /* Check the SOCK_* constants for consistency.  */
1213         BUILD_BUG_ON(SOCK_CLOEXEC != O_CLOEXEC);
1214         BUILD_BUG_ON((SOCK_MAX | SOCK_TYPE_MASK) != SOCK_TYPE_MASK);
1215         BUILD_BUG_ON(SOCK_CLOEXEC & SOCK_TYPE_MASK);
1216         BUILD_BUG_ON(SOCK_NONBLOCK & SOCK_TYPE_MASK);
1217 
1218         flags = type & ~SOCK_TYPE_MASK;
1219         if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
1220                 return -EINVAL;
1221         type &= SOCK_TYPE_MASK;
1222 
1223         if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
1224                 flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;
1225 
1226         retval = sock_create(family, type, protocol, &sock);
1227         if (retval < 0)
1228                 goto out;
```
1212到1224行之間都是在決定預設的`flags`參數應該成為什麼樣子。這裡的數個`SOCK_*`都定義在`include/linux/net.h`之中。不過，`flags`的指定方法並不影響1226行的`sock_create`呼叫。
```
1194 int sock_create(int family, int type, int protocol, struct socket **res)
1195 {
1196         return __sock_create(current->nsproxy->net_ns, family, type, protocol, res, 0);
1197 }
```
我們知道這裡直接呼叫了`__sock_create`，多加入的參數是開頭的namespace，取得當前的網路命名空間（也許當前程序在container或其他隔離環境之內），還有最後的一個0。
```
1085 int __sock_create(struct net *net, int family, int type, int protocol,
1086                          struct socket **res, int kern)
1087 {
1088         int err;
1089         struct socket *sock;
1090         const struct net_proto_family *pf;
...
1120         sock = sock_alloc();
1121         if (!sock) {
1122                 net_warn_ratelimited("socket: no more sockets\n");
1123                 return -ENFILE; /* Not exactly a match, but its the
1124                                    closest posix thing */
1125         }
1126 
1127         sock->type = type;
...
```
筆者省略了一段關於通訊協定家族（`family`）、封包型態（`type`）與欲使用協定（`protocol`）的錯誤檢查，還有一些安全性的機制。`sock_alloc`的內容是生成一個新的inode代表這個socket。`sock`本身是一個`struct socket*`型別的結構，內含有socket狀態、封包型態、種類、檔案之類的成員。
```
1129 #ifdef CONFIG_MODULES
1130         /* Attempt to load a protocol module if the find failed.
1131          *
1132          * 12/09/1996 Marcin: But! this makes REALLY only sense, if the user
1133          * requested real, full-featured networking support upon configuration.
1134          * Otherwise module support will break!
1135          */
1136         if (rcu_access_pointer(net_families[family]) == NULL)
1137                 request_module("net-pf-%d", family);
1138 #endif
1139 
1140         rcu_read_lock();
1141         pf = rcu_dereference(net_families[family]);
1142         err = -EAFNOSUPPORT;
1143         if (!pf)
1144                 goto out_release;
```
如果核心支援核心模組功能（現在幾乎所有的發行版都會預設支援這個選項）的話，那麼就確認一下這個通訊協定家族對應到的`net_proto_family`結構（在`include/linux/net.h`中）是否不在預載的核心本體之內：
```
194 struct net_proto_family {
195         int             family;
196         int             (*create)(struct net *net, struct socket *sock,
197                                   int protocol, int kern);
198         struct module   *owner;
199 };
```
如果不在的話就得從名為`net-pf*`的模組去尋找了。然後在1141行，這次是真的要提取這個結構出來，因為會需要`create`成員函數來生成要回傳的socket。
```
1156         err = pf->create(net, sock, protocol, kern);
1157         if (err < 0)
1158                 goto out_module_put;
1159 
...
1175         *res = sock;
1176 
1177         return 0;
```
單以筆者範例程式的環境，這個`create`所指的地方以及宣告的時機都在`net/ipv4/af_inet.c`之中，
```
 990 static const struct net_proto_family inet_family_ops = {
 991         .family = PF_INET,
 992         .create = inet_create,
 993         .owner  = THIS_MODULE,
 994 };
...
1754 static int __init inet_init(void)
1755 {
...
1782         (void)sock_register(&inet_family_ops);
```
`inet_create`的內容大致上是依照不同的協定與種類分別將`sock`結構的成員填好，目前就先不深究下去了。回到系統呼叫的地方：
```
1226         retval = sock_create(family, type, protocol, &sock);
1227         if (retval < 0)
1228                 goto out;
1229 
1230         retval = sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
1231         if (retval < 0)
1232                 goto out_release;
1233 
1234 out:
1235         /* It may be already another descriptor 8) Not kernel problem. */
1236         return retval;
```
我們剛結束1226的部份。1230行就是要把剛生成的`sock`結構對應到一個檔案描述子。可以預期的是裡面過程中也一定會呼叫`get_unmapped_fd`之類的呼叫、註冊檔案描述子、設定檔案描述子性質（根據`flags`）、更動程序的FDT之類的過程。但還是簡單看看：
```
 391 static int sock_map_fd(struct socket *sock, int flags)
 392 {
 393         struct file *newfile;
 394         int fd = get_unused_fd_flags(flags);
 395         if (unlikely(fd < 0))
 396                 return fd;
 397 
 398         newfile = sock_alloc_file(sock, flags, NULL);
 399         if (likely(!IS_ERR(newfile))) {
 400                 fd_install(fd, newfile);
 401                 return fd;
 402         }
 403 
 404         put_unused_fd(fd);
 405         return PTR_ERR(newfile);
 406 }
```
從這裡可以看到除了跟檔案描述子相關的操作之外，還需要有對應的檔案結構`newfile`，由`sock_alloc_file`創建。

至此，我們知道這個socket已經根據給定的協定的資訊，對應到各個協定的模組中既定義的函數，以及設定好檔案的對應了。於是在第1236行回傳一個可用的檔案描述子。

---
### 結論

先看了網路程式的第一個系統呼叫`socket`的內容。和`open`有點類似，都是一個起頭的系統呼叫，但比較不一樣的是open需要決定許多事情，甚至可能要創建檔案，對檔案也有諸般不同的處置；`socket`則是只要給定與協定相關的內容即可。接下來的幾日我們會逐漸看到其他的關鍵系統呼叫。我們明日再會！
