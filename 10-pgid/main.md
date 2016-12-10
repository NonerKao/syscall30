### 前情提要

程序管理當中很重要的一個元素即是核心如何識別程序作為一個整體的物件，其中一個程序的要件即是`struct pid`這個結構。我們在程序管理篇先是閱讀了`fork`與`wait`的應用，昨日也閱讀了`getpid`系統呼叫的實務。我們了解到我們過去理解的程序編號這個東西只是`struct pid`裡面的一部分，程序的身份識別還具有其他的性質，如所屬的程序群組以及所屬的session等而構成的兩階層式架構。本日我們就要來理解程序群組的部份。

---
### 程序群組？

為什麼會需要程序群組這種東西？根據筆者閱讀[`credential(7)`手冊](http://man7.org/linux/man-pages/man7/credentials.7.html)、[維基頁面](https://en.wikipedia.org/wiki/Job_control_(Unix))還有[GNU的libc手冊](https://www.gnu.org/software/libc/manual/html_node/Job-Control.html)的結果，這個功能就是要方便上層的login shell作**job control**的管理，在昨日的瀏覽`enum pid_type`的時候也有簡略說明。

這兩個呼叫將會操控程序群組的編號。兩者的標頭定義分別為：
```
SYNOPSIS
       #include <unistd.h>

       int setpgid(pid_t pid, pid_t pgid);
       pid_t getpgid(pid_t pid);
```
前者可以將一個程序搬移到另外一個程序群組，但是不同的程序群組必須存在同一個session之中；根據手冊，這樣的功能出現在一些shell當中用來處理pipeline相關的功能。後者則是單純的取得程序群組ID。我們分別來看這兩個系統呼叫的內容。

---
### `getpgid`
在`kernel/sys.c`中：
```
 990 SYSCALL_DEFINE1(getpgid, pid_t, pid)
 991 {               
 992         struct task_struct *p;
 993         struct pid *grp;
 994         int retval;    
 995                 
 996         rcu_read_lock();
 997         if (!pid)      
 998                 grp = task_pgrp(current);
 999         else {  
1000                 retval = -ESRCH;
1001                 p = find_task_by_vpid(pid);
1002                 if (!p)
1003                         goto out;
1004                 grp = task_pgrp(p);
1005                 if (!grp)
1006                         goto out;
1007                 
1008                 retval = security_task_getpgid(p);
1009                 if (retval)
1010                         goto out;
1011         }       
1012         retval = pid_vnr(grp);
1013 out:            
1014         rcu_read_unlock();
1015         return retval;
1016 }
1017                 
1018 #ifdef __ARCH_WANT_SYS_GETPGRP
1019                 
1020 SYSCALL_DEFINE0(getpgrp)
1021 {               
1022         return sys_getpgid(0);
1023 }
```
特地把下面的`getpgrp`也包含進來是因為手冊上將兩者寫在一起，也是因為一些歷史的因素使得Linux支援這些多種介面，當然底層的實作會盡量使這樣的多系統呼叫介面能夠用最大量的共用程式碼，這也是軟體工程的高級實踐。`getpgrp`介面的話不需要使用者傳入參數，直接呼叫`getpgid`並傳入0，這是一種預設0為當前程序的作法。

997行的判斷即是**是否為當前程序**的分岐點，取得當前程序的群組識別結構，也就是該群組的leader的對應到的PID。值得一提的是，`task_pgrp`的用法看起來似乎指向`struct pid`（程序識別用）結構是`struct task_struct`（程序本身）結構的一個成員，但實際上，在核心中表達程序用的`struct task_struct`中有的是`struct pid_link`結構，這是一個hash節點的指標指到實際存在別處的`struct pid`結構。反之，若是這個系統呼叫意圖取得別個程序所屬的群組編號，則必須多執行一步透過程序編號找到程序結構（`find_task_by_vpid`）的步驟，並且在最後必須經過核心安全模組的檢驗。無論何者，最後呼叫`pid_vnr`得到可回傳的編號而回傳，詳情可以參考之前的介紹。

核心的`task_pgrp`呼叫如下（註解為筆者翻譯）：
```
2021 /*                    
2022  * 若是沒有 tasklist 或 rcu lock 等機制保護，存取task_pgrp/task_session
2023  * 等方法都是不安全的，就算task就是當前程序。
2024  * 因為這無法保證同一個程序的其他thread會不會正在sys_setsid/sys_setpgid.
2025  */               
2026 static inline struct pid *task_pgrp(struct task_struct *task)                                                                           
2027 {       
2028         return task->group_leader->pids[PIDTYPE_PGID].pid;
2029 }
```
註解清楚地解釋了這個系統呼叫前後加上RCU的鎖的原因。本體的內容則是多層次的存取，先是程序結構內的`group_leader`成員指到另外一個程序，然後是該程序的`struct pid_link pids[]`陣列，這個陣列裡的`PIDTYPE_PGID`項目才能存取到指定的`pid`結構。如此一來，我們就能得到程序群組的編號。

順帶一題，`group_leader`成員的賦值完成於我們在`fork`一文中跳過的`copy_process`呼叫。其中有這樣的片段：
```
1558         if (clone_flags & CLONE_THREAD) {
1559                 p->exit_signal = -1;
1560                 p->group_leader = current->group_leader;                                                                                
1561                 p->tgid = current->tgid;
1562         } else {    
1563                 if (clone_flags & CLONE_PARENT)
1564                         p->exit_signal = current->group_leader->exit_signal;
1565                 else
1566                         p->exit_signal = (clone_flags & CSIGNAL);
1567                 p->group_leader = p;
1568                 p->tgid = p->pid;
1569         }           
```
也就是說，若是這個複製的過程有指定是要開啟一個新的執行緒，則程序群組的領導人應該與當前程序相同，否則就該令為自己。

---
### `setpgid`

> 開始這個部份之前，筆者必須再次謙卑地表達自己對於整個系列文的定位是學習筆記與心路歷程分享。無論是科技深度或是寫作的精熟度，筆者都在參考其他文獻時獲益良多。話說回來這個`setpgid`的部份尤其如此，在stackoverflow看到[這篇優文](http://stackoverflow.com/questions/16639275/grouping-child-processes-with-setgpid)令人嘆為觀止。雖然提問者的回饋表示他其實不需要這麼複雜的解法，但這個解答仍然充滿價值。

這個部份的呼叫一樣在`kernel/sys.c`中，因為很長所以先列出重點部份：
```
 979         if (task_pgrp(p) != pgrp)
 980                 change_pid(p, PIDTYPE_PGID, pgrp);
 981         
```
前面經過了許多錯誤判斷，進入修改程序群組的部份。這裡有著最後一個檢查，就是指定的程序的程序群組與指定的程序群組是否不同？若沒有不同則不須改變了。反之，進入`change_pid`的呼叫（在`kernel/pid.c`）：
```
395 static void __change_pid(struct task_struct *task, enum pid_type type,
396                         struct pid *new)
397 {        
398         struct pid_link *link;
399         struct pid *pid;
400         int tmp;
401          
402         link = &task->pids[type];
403         pid = link->pid;
404          
405         hlist_del_rcu(&link->node);
406         link->pid = new;
407          
408         for (tmp = PIDTYPE_MAX; --tmp >= 0; )
409                 if (!hlist_empty(&pid->tasks[tmp]))
410                         return;
411          
412         free_pid(pid);
413 }        
...
420 void change_pid(struct task_struct *task = p, enum pid_type type = PIDTYPE_PGID,                                                         
421                 struct pid *pid = pgrp)
422 {                
423         __change_pid(task, type, pid);
424         attach_pid(task, type);
425 }        
```
`change_pid`直接呼叫了`__change_pid`，首先取得了`link`為指定程序對應到的table link，然後將其原本的`pid`結構存在局部變數中。接著在405行透過`hlist`相關的巨集刪除了`link`物件原本的`hlist_node`，然後將`list`的`pid`成員指定為新的、使用者傳入的程序群組對應到的`pid`。迴圈是為了判斷是否這個`pid`結構仍然有其他的程序在使用，如果還有的話就不需要執行到412行的`free`。

`attach_pid`執行的是
```
389 void attach_pid(struct task_struct *task, enum pid_type type)                                                                       
390 {
391         struct pid_link *link = &task->pids[type];
392         hlist_add_head_rcu(&link->node, &link->pid->tasks[type]);
393 }
```
將這個`pid_link`物件加回`hlist`的過程。

---
### 結論

我們的確大概閱讀了與程序群組相關的核心結構內容以及相關資訊的提取與設定。但是仍然不免讓人懷疑，這究竟是為了什麼？也就是說，目前為止的系列文中，仍然無法給出工作控制的意義是什麼的說明。筆者在這裡提出一個簡明的答案，那就是多個程序管理得以因此變得方便。當我們日常使用終端機時，其實已經對背景/前景切換、暫停/重啟這樣的功能非常熟悉，這些控制功能都可以運用在整個程序群組上。相關的細節，在之後的訊號處理篇的`kill`等系統呼叫中會有比較多的討論。
