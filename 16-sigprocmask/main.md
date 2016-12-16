### 前情提要

`kill`是讓程序可以互相傳送訊號，`sigaction`則是讓程序可以在特定訊號來臨時執行特定行為。本日是訊號管理第三日，要來探索的是`sigprocmask`系統呼叫。

---
### 介紹

手冊`sigprocmask(2)`中如此說明：
```
NAME
       sigprocmask, rt_sigprocmask - examine and change blocked signals

SYNOPSIS
       #include <signal.h>

       /* Prototype for the glibc wrapper function */
       int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

       /* Prototype for the underlying system call */
       int rt_sigprocmask(int how, const kernel_sigset_t *set,
                          kernel_sigset_t *oldset, size_t sigsetsize);
```
這和`sigaction`一樣，都是為一個程序指定某些新的行為（第二個參數），而舊的行為也予以保留以備回覆之需（第三個參數），第四個參數也都附有一個`sigsetsize`變數作為相容性的設計之用。

與昨日一樣檢視一下`strace /bin/bash`的結果，可以看到這個系統呼叫的用法：
```
rt_sigprocmask(SIG_SETMASK, [], NULL, 8) = 0
...
rt_sigprocmask(SIG_BLOCK, [HUP INT QUIT ALRM TERM TSTP TTIN TTOU], [], 8) = 0
```
`SIG_*`巨集的用法在手冊終有提及。這裡的兩種方法分別是「沒有訊號被設定在mask中」以及「設定`[HUP INT QUIT ALRM TERM TSTP TTIN TTOU]`這八種訊號要被block住。值得注意的是，`SIGKILL`和`SIGSTOP`無法被block。

在`kernel/signal.c`之中：
```
2535 SYSCALL_DEFINE4(rt_sigprocmask, int, how, sigset_t __user *, nset,
2536                 sigset_t __user *, oset, size_t, sigsetsize)
2537 {                        
2538         sigset_t old_set, new_set;
2539         int error;       
2540                          
2541         /* XXX: Don't preclude handling different sized sigset_t's.  */
2542         if (sigsetsize != sizeof(sigset_t))
2543                 return -EINVAL;
2544                          
2545         old_set = current->blocked;
2546                          
2547         if (nset) {      
2548                 if (copy_from_user(&new_set, nset, sizeof(sigset_t)))
2549                         return -EFAULT;
2550                 sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));
2551                          
2552                 error = sigprocmask(how, &new_set, NULL);                                                                       
2553                 if (error)
2554                         return error;
2555         }                
2556                          
2557         if (oset) {      
2558                 if (copy_to_user(oset, &old_set, sizeof(sigset_t)))
2559                         return -EFAULT;
2560         }                
2561                          
2562         return 0;        
2563 }                        
```
果然結構也是類似的，`sigdelsermask`呼叫昨日也看到過。核心的`sigprocmask`呼叫的內容涉及一些switch-case的判斷：
```
2501 int sigprocmask(int how, sigset_t *set, sigset_t *oldset)
2502 {      
2503         struct task_struct *tsk = current;
2504         sigset_t newset;
2505        
2506         /* Lockless, only current can change ->blocked, never from irq */
2507         if (oldset)
2508                 *oldset = tsk->blocked;
2509        
2510         switch (how) {
2511         case SIG_BLOCK:
2512                 sigorsets(&newset, &tsk->blocked, set);
2513                 break;
2514         case SIG_UNBLOCK:
2515                 sigandnsets(&newset, &tsk->blocked, set);
2516                 break;
2517         case SIG_SETMASK:
2518                 newset = *set;
2519                 break;
2520         default:
2521                 return -EINVAL;
2522         }
2523        
2524         __set_current_blocked(&newset);
2525         return 0;
2526 }
```
就是根據手冊的內容實作而已。如當`SIG_BLOCK`出現時，已經block的訊號繼續block，且另外新增新傳入的`newset`。過程中出現的`sigorsets`和`sigandnsets`兩個呼叫定義在`include/linux/signal.h`中，只是單純的真偽運算：
```
128 #define _sig_or(x,y)    ((x) | (y))
129 _SIG_SET_BINOP(sigorsets, _sig_or)                                                                                                       
130                 
131 #define _sig_and(x,y)   ((x) & (y))
132 _SIG_SET_BINOP(sigandsets, _sig_and)
133                 
134 #define _sig_andn(x,y)  ((x) & ~(y))
135 _SIG_SET_BINOP(sigandnsets, _sig_andn)
```

```
### 結論

今天的程序處理函數相對容易，我們明天將把程序管理告一段落，然後來檢查核心內部傳遞signal處理的方式！

