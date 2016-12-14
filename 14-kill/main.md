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

---
### 結論
