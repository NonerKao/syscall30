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

`wait4`在`kernel/exit.c`中，
```

```



