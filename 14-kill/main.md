### 前情提要

程序管理的部份告一段落，我們在過程中看到許多與signal相關的事件，因此接下來的單元準備介紹signal相關的系統呼叫。

---
### signal為何物？

推薦讀者參考`signal(7)`手冊。簡單來說，這是一套POSIX標準規定的介面，可以改變程序的行為，也提供程序因應signal的諸般方法。最基本的就是程序可以設定signal mask決定哪些訊號可以接收，再進階一些的則有像是可以對不同的訊號註冊不同的中斷程序(signal handler)。

---
### `kill`系統呼叫

```
NAME
       kill - send signal to a process

SYNOPSIS
       #include <sys/types.h>
       #include <signal.h>

       int kill(pid_t pid, int sig);
```
`kill`呼叫大概是字面上意義與實際意義相差最遠的一個系統呼叫了，雖然名為kill，實際的功能卻更通用，就是傳送signal給編號為`pid`的process。`raise(3)`這個API的行為也是使用`kill(getpid(), sig)`來實作的。

這個系統呼叫的定義在`kernel/signal.c`裡面：
```
2847 SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
2848 {
2849         struct siginfo info;
2850 
2851         info.si_signo = sig;
2852         info.si_errno = 0;
2853         info.si_code = SI_USER;
2854         info.si_pid = task_tgid_vnr(current);
2855         info.si_uid = from_kuid_munged(current_user_ns(), current_uid());
2856 
2857         return kill_something_info(sig, &info, pid);
2858 }
```
顯然的這個call就是先準備好一個`struct siginfo`結構。`signo`就是傳入的signal號碼，`errno`錯誤編號預設是0，最後這個`SI_USER`代表準備這個結構為的是處理由使用者空間發起的訊號。都準備好了之後，呼叫`kill_something_info`。這個結構位在`include/uapi/asm-generic/siginfo.h`裡面，
```
 48 typedef struct siginfo {                                                                                                                  
 49         int si_signo;
 50         int si_errno;
 51         int si_code;
 52  
 53         union {
...
```
除了這前三個成員之外，所有其他的成員都在內含的`union _sifields`中。所以2854、2855行直接引用`si_xid`的用法是怎麼回事呢？又是巨集魔法，學了一招：
```
124 /*                                                                                                                                        
125  * How these fields are to be accessed.
126  */
127 #define si_pid          _sifields._kill._pid
128 #define si_uid          _sifields._kill._uid
...
```
`_sifields`裡面包含了許多不同情境下的`siginfo`用途，這裡因為是由`kill`系統呼叫所需要，所以使用的是裡面的`struct _kill`結構。

大部分的情況下，`kill_something_info`的呼叫會走到這個判斷就結束：
```
1374 static int kill_something_info(int sig, struct siginfo *info, pid_t pid)
1375 {       
1376         int ret;
1377         
1378         if (pid > 0) {
1379                 rcu_read_lock();
1380                 ret = kill_pid_info(sig, info, find_vpid(pid));
1381                 rcu_read_unlock();
1382                 return ret;
1383         }
...
```
`kill_pid_info`接下來會呼叫`group_send_sig_info`，差異在於將程序編號透過檢索的過程轉換成了`struct pid`，然後透過其中的`check_kill_permission`檢查權限是否相符之後，`do_send_sig_info`內的`send_signal`會將傳送該訊號，於是完成`kill`的訊號傳送目的。

---
### 結論

`kill`系統呼叫的行為可說是訊號管理的基本。我們在本文中看到了這個過程的一些步驟與資料結構，接下來會從訊號管理的其他系統呼叫陸續看到這個機制的實作是什麼樣子。我們明天再會！
