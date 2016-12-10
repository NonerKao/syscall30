### 前情提要

進入了程序管理的部份之後，我們已經閱讀過`fork`和`wait`。雖然程序也是抽象的物件，但是相比於檔案處理時所需要的抽象，這個部份令人感到具體許多，操作上和程式碼的解讀都比較容易。然而我們也看到許多變體，如在Linux環境更萬用的`clone`系統呼叫或是其他的`wait`相關呼叫等等。也就是說我們仍然有許多功能未曾觸及。

為了接下來的`clone`、`execve`等呼叫鋪路，也為了將前兩日看到的`struct pid`弄的更清楚些，筆者決定從今天開始連續幾天瀏覽一些與程序管理相關而又比較單純的系統呼叫，從取得資訊用的`getxid`，到設定用的`setxxx`。

---
### 介紹

本文要介紹的系統呼叫為`getpid`及`getppid`。最直接介紹這些的，是參考手冊的`credential(7)`。字面上，credential的意思是憑證、認證的意思，這份手冊主要則是在說明程序的相關資訊以及檔案屬性的識別資訊。本文的重點當然是前半的部份。

之前在`fork`或`wait`的使用上必須使用到程序的編號，因此提前使用過`getpid`這樣的呼叫；然而，在`fork`一文當中，我們也看到過系統呼叫的函式內有一個令人在意的`PIDTYPE_PID`這個項，它還有其他兄弟一起被定義在`include/linux/pid.h`中：
```
  6 enum pid_type                          
  7 {        
  8         PIDTYPE_PID,                                                                                                                     
  9         PIDTYPE_PGID,
 10         PIDTYPE_SID,                   
 11         PIDTYPE_MAX
 12 };                       
```
手冊中說PID就是對應到程序編號，這很容易理解；PGID，則是程序群組（process group）的編號，這樣的單位可以方便shell執行工作控制（job control）的機制，比方說如果執行一連串的pipe指令如`cat file.txt | sort -k2 | awk '{print $3}'`之類的，就會同屬一個程序群組。一個session則是共享一個終端機的所有程序的集合，這些程序都會擁有一樣的SID。這就為程序的編號型成一個兩個階層的結構。

---
### `getpid`與`getppid`
這兩個系統呼叫在`kernel/sys.c`中（先以`getpid`的角度解析）：
```
 830 SYSCALL_DEFINE0(getpid)
 831 {                
 832         return task_tgid_vnr(current);
 833 }                
 834                  
 ...
 847 SYSCALL_DEFINE0(getppid)                                                                                                                 
 848 {                
 849         int pid; 
 850                  
 851         rcu_read_lock();
 852         pid = task_tgid_vnr(rcu_dereference(current->real_parent));
 853         rcu_read_unlock();
 854                  
 855         return pid;
 856 }                
```
`current`是我們已經見過許多次的當前程序，當這個`struct task_struct*`指標傳入位在`include/linux/sched.h`的`task_tgid_vnr`之後，
```
2065 static inline pid_t task_pid_vnr(struct task_struct *tsk)
2066 {               
2067         return __task_pid_nr_ns(tsk, PIDTYPE_PID, NULL);                                                                                 
2068 }               
```
再呼叫了`kernel/pid.c`裡面的`__task_pid_nr_ns`（以下在函數標頭註記傳入值以方便理解）：
```
520 pid_t __task_pid_nr_ns(struct task_struct *task = current, enum pid_type type = PIDTYPE_PID,                                           
521                         struct pid_namespace *ns = NULL)
522 {               
523         pid_t nr = 0;
524                 
525         rcu_read_lock();
526         if (!ns)
527                 ns = task_active_pid_ns(current);
528         if (likely(pid_alive(task))) {
529                 if (type != PIDTYPE_PID) 
530                         task = task->group_leader;
531                 nr = pid_nr_ns(rcu_dereference(task->pids[type].pid), ns);
532         }       
533         rcu_read_unlock();
534                 
535         return nr;
536 }
```
527行會從`current`提取該程序內部的`pid`結構，再根據該結構提取相關的命名空間資訊，在呼叫`getpid`的時候必然會進入這裡。然後如果這個工作還活著，那麼呼叫`pid_nr_ns`，這在[`fork`一文](http://ithelp.ithome.com.tw/articles/10185342)曾經提及，會回傳指定的程序編號。

`getppid`的執行模式也沒有什麼不同，除了必須要使用RCU機制之外。筆者不由得想起笛卡爾的我思故我在之名言，因為當前程序使用了系統呼叫，所以確定當前程序一定是存在的，但是這時候的parent的狀態如何，就很難說了。

> 關於`real_parent`與`parent`的不同，請參考[這篇wiki](https://en.wikipedia.org/wiki/Parent_process#Linux)

簡單來使用個範例程式：
```
#include <stdio.h>
  2 #include <sys/types.h>
  3 #include <unistd.h>
  4          
  5 int main(){
  6         fork();                                                                                                                           
  7         printf("This process is %d\n", getpid());
  8         printf("The parent process is %d\n", getppid());
  9 }        
```
執行結果會類似這樣：
```
[noner@heros 9-getxid-setxid]$ ./pid
This process is 12187
The parent process is 4746
This process is 12188
The parent process is 12187
```

---
### 結論

`credential(7)`手冊提供了非常多資訊，建議各位讀者都能閱讀看看。程序管理的篇章結束之後，我們還會在之後的權限管理部份回來瀏覽這份文件。本日閱讀了`getpid`與`getppid`兩者，這個週末會接著看`getpgid`、`setpgid`、`getsid`、`setsid`等呼叫。這些都結束之後，想必會讓各位讀者對於Linux看待程序以及執行緒的方式更有感覺，那麼再來介紹`clone`呼叫想必會較為合適。

鐵人賽的第二個週末也要持續衝刺，我們明天再會！
