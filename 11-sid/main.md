### 前情提要

延續前兩日的程序識別探討，本文要完成程序識別的二層級架構的的最高層，也就是共用一個終端機的程序共同所在的單位：session。

---
### 標頭定義
先看`getsid`再看`setsid`。
```
NAME
       getsid - get session ID

SYNOPSIS
       #include <unistd.h>

       pid_t getsid(pid_t pid);
```
與昨日的`getpgid`類似，要指定一個程序編號（或是輸入零直接視為當前程序）。
```
NAME
       setsid - creates a session and sets the process group ID

SYNOPSIS
       #include <unistd.h>

       pid_t setsid(void);
```
這就和`setpgid`大異其趣了！`setsid`是不須參數的。根據手冊，setsid之後，呼叫的程序就會自成一家，變成自己的session leader，我們後面會看到這是怎麼作的。

---
### session到底是什麼？

同[第九日](http://ithelp.ithome.com.tw/articles/10185515)的討論，session即是一群共用同一個終端機的process。我們可以透過指令觀察：
```
[noner@heros ~]$ ps -eo pid,pgid,sid,tty,user,comm -f
  PID  PGID   SID TT       USER     COMMAND
12103 12103 12103 pts/7    noner    bash
11809 11809 11809 pts/6    noner    bash
11624 11624 11624 pts/2    noner    bash
11629 11629 11624 pts/2    noner     \_ man
11641 11629 11624 pts/2    noner         \_ less
10955 10955 10955 pts/5    noner    bash
12075 12075 10955 pts/5    noner     \_ a.out
10708 10708 10708 pts/4    noner    bash
 3644  3644  3644 pts/3    noner    bash
10697 10697  3644 pts/3    noner     \_ vim
 2371  2371  2371 pts/1    noner    bash
13488 13488  2371 pts/1    noner     \_ ps
 1244  1244  1244 pts/0    noner    bash
10944 10944  1244 pts/0    noner     \_ vim
...
```
我們可以看到，一般來說若沒有特別要求，每一個程序都會代表自己的程序群組；這裡的例外是位在pts/2終端機的手冊閱讀程式，它使用less作為文章翻頁的工具，所以這兩個（`man`與`less`）程序的PGID同屬程序編號為11629的`man`程序。說到SID，那就是都和所屬的虛擬終端機的bash程序相同了。

---
### `getsid`
先說結論，這和`getpgid`幾乎一模一樣。在`kernel/sys.c`之中：
```
1027 SYSCALL_DEFINE1(getsid, pid_t, pid)
1028 {                      
1029         struct task_struct *p;
1030         struct pid *sid; 
1031         int retval;    
1032                        
1033         rcu_read_lock();
1034         if (!pid)      
1035                 sid = task_session(current);
1036         else {         
1037                 retval = -ESRCH;
1038                 p = find_task_by_vpid(pid);
1039                 if (!p)
1040                         goto out;
1041                 sid = task_session(p);
1042                 if (!sid)
1043                         goto out;
1044                        
1045                 retval = security_task_getsid(p);
1046                 if (retval)
1047                         goto out;
1048         }              
1049         retval = pid_vnr(sid);
1050 out:                   
1051         rcu_read_unlock();
1052         return retval; 
1053 }                      
```
連`task_session`都在`task_pgrp`的下面而已，長的也一樣：
```
2031 static inline struct pid *task_session(struct task_struct *task)
2032 {       
2033         return task->group_leader->pids[PIDTYPE_SID].pid;
2034 }       
```
所以筆者認為這裡應該沒有什麼好說的吧。請參考[昨日](http://ithelp.ithome.com.tw/articles/10185584)的文章。

---
### `setsid`
```
1066 SYSCALL_DEFINE0(setsid)
1067 {               
1068         struct task_struct *group_leader = current->group_leader;
1069         struct pid *sid = task_pid(group_leader);
1070         pid_t session = pid_vnr(sid);
1071         int err = -EPERM;
1072         
1073         write_lock_irq(&tasklist_lock);
1074         /* Fail if I am already a session leader */
1075         if (group_leader->signal->leader)
1076                 goto out;
1077         
1078         /* Fail if a process group id already exists that equals the
1079          * proposed session id.
1080          */
1081         if (pid_task(sid, PIDTYPE_PGID))
1082                 goto out;
1083         
1084         group_leader->signal->leader = 1;
1085         set_special_pids(sid);
1086         
1087         proc_clear_tty(group_leader);
1088         
1089         err = session;
1090 out:    
1091         write_unlock_irq(&tasklist_lock);
1092         if (err > 0) {
1093                 proc_sid_connector(group_leader);
1094                 sched_autogroup_create_attach(group_leader);
1095         }
1096         return err;
1097 }
```
到1083行為止都做了一些錯誤條件的判斷，如這個程序的群組頭目是否原本就是一個session的頭目（1075行的判斷）（以程序結構的`signal`成員的`leader`作為真偽值判斷）；1084行就使得這個群組頭目成為真，然後是1085行的`set_special_pids`（就在`setsid`系統呼叫上方）：
```
1055 static void set_special_pids(struct pid *pid)
1056 {       
1057         struct task_struct *curr = current->group_leader;
1058                                                                                                                                         
1059         if (task_session(curr) != pid)
1060                 change_pid(curr, PIDTYPE_SID, pid);
1061             
1062         if (task_pgrp(curr) != pid)
1063                 change_pid(curr, PIDTYPE_PGID, pid);
1064 }           
```
一般情況之下，這時候當前程序臨危受命要成為session leader，所以它通常是個程序群組的頭目，不太可能進入1062的判斷，但是1059的判斷就很有機會進入了。我們昨日看過`change_pid`了，這裡就略過。

1087行有個`proc_clear_tty`，字面上看來是要清除程序的虛擬終端機，詳情究竟如何（在`drivers/tty/tty_io.c`）：
```
 512 void proc_clear_tty(struct task_struct *p)
 513 {                                                                                                                                       
 514         unsigned long flags;
 515         struct tty_struct *tty;
 516         spin_lock_irqsave(&p->sighand->siglock, flags);
 517         tty = p->signal->tty;
 518         p->signal->tty = NULL;
 519         spin_unlock_irqrestore(&p->sighand->siglock, flags);
 520         tty_kref_put(tty);
 521 }       
```
先將這個程序`p`的`tty`成員儲存起來之後，隨即將之另為`NULL`。存起來是為了後面的`*put`相關函數對核心物件的存取數量的控制，若是小於1甚至會需要將相關的記憶體空間free掉，總之是這麼一回事。

---
### 與終端機關係？

請參考這個範例程式：
```
  1 #include <stdio.h>
  2 #include <sys/types.h>
  3 #include <unistd.h>
  4                 
  5 int main(int argc, char *argv[], char *envp[]){
  6                 
  7         pid_t pid;
  8                                                                                                                                           
  9         printf("This process is of session %d\n", getsid(0));
 10         if((pid = fork()) == 0){
 11                 setsid();
 12                 system("bash");
 13         } else {
 14                 waitpid(pid, NULL, 0);
 15         }       
 16         printf("Process %d if of session %d\n", getpid(), getsid(0));
 17 }  
```
10行`fork`之後，讓子程序執行一個`setsid`自立門戶，然後讓它再執行一個`bash`（這裡面其實也牽涉到一個`fork`和一個`execve`，日後會介紹）。我們可以在新的bash裡面執行一些操作，以證明這個新的session有些不同的性質。編譯並執行之，
```
[noner@heros 11-sid]$ ps
  PID TTY          TIME CMD
 2371 pts/1    00:00:00 bash
 3872 pts/1    00:00:00 ps
[noner@heros 11-sid]$ ./a.out 
This process is of session 2371
bash: cannot set terminal process group (3874): Inappropriate ioctl for device
bash: no job control in this shell
[noner@heros 11-sid]$ 
```
這個錯誤訊息應該是是system("bash")造成的。它的父程序自立門戶之後，卻沒有給予終端機的存取，因此這個bash感受到了它的執行環境不太適合job control的功能。我們若在這裡看看深入的ps訊息：
```
[noner@heros 11-sid]$ ps auxf
...
noner     2371  0.0  0.0  20816  3748 pts/1    Ss   Dec10   0:00  |   \_ bash
noner     3873  0.0  0.0   4160   624 pts/1    S+   00:01   0:00  |   |   \_ ./a.out
noner     3874  0.0  0.0   4160    72 ?        Ss   00:01   0:00  |   |       \_ ./a.out
noner     3875  0.0  0.0  21020  3456 ?        S    00:01   0:00  |   |           \_ bash
noner     3906  0.0  0.0  39980  3740 ?        R    00:05   0:00  |   |               \_ ps auxf
...
```
被fork出來的3874程序開始，終端機的欄位就沒有繼承`pts/1`了，這就是`setsid`的結果。

---
### 結論

若說`getxid`是一般程序都可以使用的系統呼叫以取得程式關心的資訊，那麼`setpgid`或`setsid`系統呼叫就是專門由系統程式使用的服務，因為這些系統程式需要直接地管理多個程序與他們的行為。從階層最高的session的控制來看，`setsid`就是專門由需要開啟或管理終端機的程式所使用，如sshd或一些虛擬終端機的客戶端；`setpgid`就是專門由shell使用來作工作控制。

探討這部份的功能時，最好的學習方法應該是結合這些使用者空間的工具。我們沒有那麼作，而是專門閱讀核心部份如何架構抽象物件之間的關聯，使得核心能夠支援那些功能。接下來我們會探討`clone`和`exit`結束程序管理篇章，然後進入其他的部份。
