### 前情提要

昨日非常簡略地帶過`clone`呼叫，在程序管理篇章的最後，介紹中止程序，也就是`exit`相關的系統呼叫。

---
### 介紹

分別看看`exit(3)`和`exit_group(2)`的手冊：
```
NAME
       exit - cause normal process termination

SYNOPSIS
       #include <stdlib.h>

       void exit(int status);
```
和，
```
NAME
       exit_group - exit all threads in a process

SYNOPSIS
       #include <linux/unistd.h>

       void exit_group(int status);
```
稍微敏感一點的讀者應該有注意到或是早就知道，其實這個`exit(3)`指的是API的呼叫，其實在Linux底下，這還是使用`exit_group`系統呼叫的。我們可以使用`strace -f`模式（能觀察fork出的子程序及執行緒）於昨日的範例程式，得到下面片段：
```
[pid 17662] exit(0)                     = ?
[pid 17662] +++ exited with 0 +++
[pid 17661] <... futex resumed> )       = 0
[pid 17661] exit_group(0)               = ?
[pid 17661] +++ exited with 0 +++
<... wait4 resumed> [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 17661
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_EXITED, si_pid=17661, si_uid=1001, si_status=0, si_utime=0, si_stime=0} ---
exit_group(0)                           = ?
```
17662是子程序產生的執行緒，`pthread_join`內部採用`futex`等待之，結束的手法是`exit`；17661則是子程序本身，使用的結束呼叫則是`exit_group`這個系統呼叫。這兩個呼叫都出現在`kernel/exit.c`中，我們可以分別看看他們的內容。

`exit`長成這樣：
```
 920 SYSCALL_DEFINE1(exit, int, error_code)
 921 {                                                                                                                                       
 922         do_exit((error_code&0xff)<<8);
 923 }
```
`exit_group`則是這樣：
```
 957 /*             
 958  * this kills every thread in the thread group. Note that any externally
 959  * wait4()-ing process will get the correct exit code - even if this
 960  * thread is not the thread group leader.
 961  */            
 962 SYSCALL_DEFINE1(exit_group, int, error_code)                                                                                     
 963 {              
 964         do_group_exit((error_code & 0xff) << 8);
 965         /* NOTREACHED */
 966         return 0;
 967 }
```

---
### 進一步

這個函式會用signal通知thread group裡面的其他thread使之結束：
```
 925 /*      
 926  * Take down every thread in the group.  This is called by fatal signals
 927  * as well as by sys_exit_group (below).
 928  */     
 929 void    
 930 do_group_exit(int exit_code)
 931 {       
 932         struct signal_struct *sig = current->signal;
 933         
 934         BUG_ON(exit_code & 0x80); /* core dumps don't get here */
 935         
 936         if (signal_group_exit(sig))
 937                 exit_code = sig->group_exit_code;
 938         else if (!thread_group_empty(current)) {
 939                 struct sighand_struct *const sighand = current->sighand;
 940         
 941                 spin_lock_irq(&sighand->siglock);
 942                 if (signal_group_exit(sig))
 943                         /* Another thread got here before we took the lock.  */
 944                         exit_code = sig->group_exit_code;
 945                 else {
 946                         sig->group_exit_code = exit_code;
 947                         sig->flags = SIGNAL_GROUP_EXIT;
 948                         zap_other_threads(current);
 949                 }
 950                 spin_unlock_irq(&sighand->siglock);
 951         }
 952         
 953         do_exit(exit_code);
 954         /* NOTREACHED */
 955 }       
```
並且在最後執行不會回傳的`do_exit`函式。這是一個大概120行的函式，做了許多的收尾工作，如設定結束的signal、釋放記憶體用量、結算統計數據等等。在最後有一個`schedule()`函數會引發CPU的交棒，執行另外一個程式。

---
### 結論

由於整體的認識不足，使得程序管理篇的`clone`和`exit`有點過於倉促。希望這個部份可以在後續增加對signal以及排程管理的認識之後陸續補足！明天我們就會開始新的篇章，筆者會拿捏一下要從記憶體的相關呼叫開始或是signal的開始。無論如何，我們明天再會！

