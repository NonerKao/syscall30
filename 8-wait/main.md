### 前情提要

介紹了`fork`（及進階功能的`clone`）這個經典的系統呼叫，以及核心的程序管理的部份功能。參考以下範例程式碼：
```
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(){
	
	pid_t pid = fork();

	if(pid)	{
		int temp;
		scanf("%d", &temp);
	}

	return 0;
}
```
趁著父程序卡住的時候，偷偷看一下全系統的程序列表：
```
demo      8850  0.0  0.0   4160   640 pts/5    S+   13:42   0:00  |   |   \_ ./a.out
demo      8851  0.0  0.0      0     0 pts/5    Z+   13:42   0:00  |   |       \_ [a.out] <defunct>
```
可以發現呼叫的`a.out`的子程序有一個附加說明：`<defunct>`，也就是那些沒有處理掉的無用程序。所以今天要講搭配用的系統呼叫：`wait`，從父程序的角度觀察如何控制產生的子程序。

> 對於殭屍行為有興趣的讀者，請參考`wait(2)`的手冊的`NOTE`一節，有非常清楚的解說。

---
### `wait`介紹

與之前介紹的幾個呼叫相比，`wait`在使用者空間的wrapper相當多采多姿。參考手冊，就能看到整個家族：
```
NAME
       wait, waitpid, waitid - wait for process to change state

SYNOPSIS
       #include <sys/types.h>
       #include <sys/wait.h>

       pid_t wait(int *wstatus);

       pid_t waitpid(pid_t pid, int *wstatus, int options);

       int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
                       /* This is the glibc and POSIX interface; see
                          NOTES for information on the raw system call. */
```
這是用`wait(2)`去找的結果。若是用`wait4(2)`去找，則：
```
NAME
       wait3, wait4 - wait for process to change state, BSD style

SYNOPSIS
       #include <sys/types.h>
       #include <sys/time.h>
       #include <sys/resource.h>
       #include <sys/wait.h>

       pid_t wait3(int *wstatus, int options,
                   struct rusage *rusage);

       pid_t wait4(pid_t pid, int *wstatus, int options,
                   struct rusage *rusage);
```
似乎這裡又有一些POSIX和BSD之間的愛恨情仇？筆者無意深究這個部份，我們直接先來看看一個範例程式，然後用`strace`觀察glibc如何轉譯wrapper、引用了哪個Linux系統呼叫。考慮這個範例：
```
  1 #include <stdio.h>
  2 #include <sys/types.h>
  3 #include <sys/time.h>
  4 #include <sys/resource.h>
  5 #include <sys/wait.h>
  6 #include <unistd.h>
  7 
  8 int main(){
  9         int number = 0;
 10         int wstatus;
 11 
 12         pid_t pid = fork();
 13 
 14         if(pid > 0){
 15                 waitpid(pid, &wstatus, 0);
 16                 if(WIFEXITED(wstatus)) 
 17                         printf("The child exits.\n");
 18         }               
 19         else{
 20                 scanf("%d", &number);
 21         }       
 22 
 23         printf("%d before return with %d\n", getpid(), number);
 24         return number;
 25 }
```
因為`fork`回傳的值有3種情況，所以14行的地方要有大於0（父程序成功回傳）的判斷。19~21行的地方，我們為了觀察一些行為，讓子程序會停下來等待一個輸入；14~18的父程序的部份，使用waitpid這個常用的wrapper，傳入欲等待的子程序、欲取得的回傳資訊`wstatus`、以及最後不特別指定的某個狀態值。`WIFEXITED`則是一整組通稱`W*`系列的巨集，用來判斷等待到的子程序狀態，這裡判斷的是子程序結束與否。

使用`strace`追蹤這個程式，則會得到：
```
[demo@linux 8-wait]$ strace -f ./a.out 
execve("./a.out", ["./a.out"], [/* 40 vars */]) = 0
...
clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f939606a710) = 12918
strace: Process 12918 attached
[pid 12917] wait4(12918,  <unfinished ...>
[pid 12918] fstat(0, {st_mode=S_IFCHR|0620, st_rdev=makedev(136, 6), ...}) = 0
[pid 12918] brk(NULL)                   = 0xd2f000
[pid 12918] brk(0xd50000)               = 0xd50000
[pid 12918] read(0,
```
這時候會因為子程序卡在讀取，隨意輸入一個數字之後，
```
[pid 12918] read(0, 123
"123\n", 1024)      = 4
...
[pid 12918] +++ exited with 0 +++
<... wait4 resumed> [{WIFEXITED(s) && WEXITSTATUS(s) == 123}], 0, NULL) = 12918
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_EXITED, si_pid=12918, si_uid=1001, si_status=0, si_utime=0, si_stime=0} ---

```
可見glibc往下呼叫的是`wait4`這個系統呼叫了；在這個例子，前三個參數都與`waitpid`的三個參數對應，最後一個則是`NULL`表示不做其他要求。

---
### `wait4`

`wait4`在`kernel/exit.c`中，我們分三段來看：
```
1674 SYSCALL_DEFINE4(wait4, pid_t, upid, int __user *, stat_addr,
1675                 int, options, struct rusage __user *, ru)
1676 {       
1677         struct wait_opts wo;
1678         struct pid *pid = NULL;
1679         enum pid_type type;
1680         long ret;
1681         
1682         if (options & ~(WNOHANG|WUNTRACED|WCONTINUED|
1683                         __WNOTHREAD|__WCLONE|__WALL))
1684                 return -EINVAL;
```
傳入的四個參數分別是程序的編號、狀態變數的位址、額外選項以及waitpid沒有的資源使用結構。一開始先判斷是否有未定義的選項出現，上面三個是歷史比較悠久的flag，下面這個則是Linux 4.7才加入的功能，目的是為了提供`clone`產生的執行緒也能夠有等待的機制去管理。
```
1686         if (upid == -1)
1687                 type = PIDTYPE_MAX;
1688         else if (upid < 0) {
1689                 type = PIDTYPE_PGID;
1690                 pid = find_get_pid(-upid);
1691         } else if (upid == 0) {
1692                 type = PIDTYPE_PGID;
1693                 pid = get_task_pid(current, PIDTYPE_PGID);
1694         } else /* upid > 0 */ {
1695                 type = PIDTYPE_PID;
1696                 pid = find_get_pid(upid);
1697         }
```
中段的部份，測試傳入的程序編號形式，這個部份在使用手冊有完整定義（`waitpid(2)`）：
```
       The value of pid can be:

       < -1   meaning wait for any child process whose process group ID is equal to the absolute value of pid.

       -1     meaning wait for any child process.

       0      meaning wait for any child process whose process group ID is equal to that of the calling process.

       > 0    meaning wait for the child whose process ID is equal to the value of pid.
```
我們使用的這個範例程式會進入最後一個`else`的範圍，因為我們有確實指定`upid`。`find_get_pid`這個呼叫可以把一個編號轉換成`pid`結構，
```
（kernel/pid.c中）
488 struct pid *find_get_pid(pid_t nr)
489 {              
490         struct pid *pid;
491                
492         rcu_read_lock();
493         pid = get_pid(find_vpid(nr));                                                                                                    
494         rcu_read_unlock();
495                
496         return pid;
497 }              
```
內容是先用RCU機制保護之後，呼叫`find_vpid`，也就是尋找經過namespace虛擬化的程序編號，這個階段就會回傳一個`pid`結構，
```
366 struct pid *find_pid_ns(int nr, struct pid_namespace *ns)
367 {      
368         struct upid *pnr;
369        
370         hlist_for_each_entry_rcu(pnr,
371                         &pid_hash[pid_hashfn(nr, ns)], pid_chain)
372                 if (pnr->nr == nr && pnr->ns == ns)
373                         return container_of(pnr, struct pid,
374                                         numbers[ns->level]);
375        
376         return NULL;
377 }      
378 EXPORT_SYMBOL_GPL(find_pid_ns);
379        
380 struct pid *find_vpid(int nr)
381 {      
382         return find_pid_ns(nr, task_active_pid_ns(current));
383 }      
```
先進入380行之後，轉呼叫`find_pid_ns`，傳入nr（待搜尋的程序編號）以及當前工作運行的PID NS；`find_pid_ns`則是跑一個大迴圈，把系統紀錄的每一個`pid`結構拿出來判斷，簡單來說就是針對編號和命名空間兩項檢索。若是找到了，則回傳一個常用巨集`container_of`套用在這個結構。

> `container_of(A, B, C)`是個非常重要的巨集，其中B是一個結構體而C是內部的成員，A是C真正存在的位址，那麼那個對應的B結構的位址再哪裡？有這種需求就可以使用這個巨集了。

全部回傳之後還要使用`get_pid`，其內只是單純一個增加存取數量的指令。

最後一段：
```
1699         wo.wo_type      = type;
1700         wo.wo_pid       = pid;
1701         wo.wo_flags     = options | WEXITED;
1702         wo.wo_info      = NULL;
1703         wo.wo_stat      = stat_addr;
1704         wo.wo_rusage    = ru;
1705         ret = do_wait(&wo);
1706         put_pid(pid);  
1707                        
1708         return ret;    
```
相對無聊的一個段落，前面就是在填補`wo`也就是`wait_option`結構的一個變數，然後作`do_wait`呼叫。這個呼叫主要會將當前的工作加入wait queue中，然後執行一個不斷檢查所屬子程序或是thread的迴圈，結束之後重新設定當前程序的狀態為RUNNING，並將之從wait queue中移除。

---
### 結論

這個呼叫有很多種變形，也牽涉到scheduler以及程序的執行狀態。下一次我們連同`clone`一起講解，明天再會！
