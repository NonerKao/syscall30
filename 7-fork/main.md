### 前情提要

基本的檔案操作大致瀏覽完，本篇開始預計介紹四、五個與程序管理有關的系統呼叫。

---
### `fork`介紹

若讀者修過作業系統，則應該對這個系統呼叫最熟悉，因為這不像檔案操作那樣還隔了一層C函式庫的介面。程序管理是第一線的系統軟體需要配置更多資源的時候做的事情，因此在作業系統課程介紹原理的時候，往往就會直接引用這個系統呼叫的範例，簡單明瞭。

一如往常我們先檢查手冊：
```
NAME
       fork - create a child process

SYNOPSIS
       #include <unistd.h>

       pid_t fork(void);
```
自`fork`呼叫回傳之後，整個程式的執行流程就會一分為二，父程序得到的回傳值（型別為`pid_t`）是子程序的程序ID，子程序則得到0的回傳值。因此傳統的使用者空間作法如下：
```
pid_t pid = fork();

if(pid == 0) {
	/*child process*/
} else {
	/*parent process*/
}
```
父程序有一些方法可以控制子程序，最基本的`wait`系統呼叫我們近日就會看到了。

---
### 靜態追蹤

核心裡面的實作如何呢？在`kernel/fork.c`之中：
```
1846 #ifdef __ARCH_WANT_SYS_FORK
1847 SYSCALL_DEFINE0(fork)
1848 {       
1849 #ifdef CONFIG_MMU                                                                                                                
1850         return _do_fork(SIGCHLD, 0, 0, NULL, NULL, 0);
1851 #else   
1852         /* can not support in nommu mode */
1853         return -EINVAL;
1854 #endif  
1855 }       
1856 #endif  
```
這不是疊床架屋的結構，因為`_do_fork`由`fork`、`vfork`以及`clone`等系統呼叫共用。註解的內容說明，如果沒有記憶體管理單元（MMU）的話，就無法執行`_do_fork`呼叫。這點可以參考[這篇文章](http://stackoverflow.com/questions/4856255/the-difference-between-fork-vfork-exec-and-clone)的說明。簡單來說就是現在的`fork`呼叫會有copy on write的行為，因此需要MMU支援。

> 必須注意的是，這篇文章也有提到現在的`fork`呼叫在Linux內會導向`clone`系統呼叫，這是正確的，但是因為`clone`所作的事情非常多，參數量也相當龐大，因此筆者打算將`clone`留到程序管理系列的後面再行討論。

`_do_fork`很長，我們分段來看（筆者將註解翻譯了一下，也將fork的參數帶入）：
```
1747  * 好的，這就是fork的主要函式。
1748  *              
1749  * 這個函式複製整個程序、成功地啟用該程序、
1750  * 並如果必要的話，等待虛擬記憶體的使用結束。
1751  */             
1752 long _do_fork(unsigned long clone_flags = SIGCHLD,
1753               unsigned long stack_start = 0,
1754               unsigned long stack_size = 0,
1755               int __user *parent_tidptr = NULL,
1756               int __user *child_tidptr = NULL,
1757               unsigned long tls = 0)
1758 {               
1759         struct task_struct *p;
1760         int trace = 0;
1761         long nr;
```
局部變數的`task_struct`是程序管理最重要的一個結構體，非常龐大，在`include/linux/sched.h`之中定義。`trace`代表這個新的程序或是執行緒是否被追蹤的狀態，而`nr`是將要回傳的程序ID。
```
1763         /*      
1764          * 決定應該回報何種事件給予ptracer...
...
1768          */     
1769         if (!(clone_flags & CLONE_UNTRACED)) {
1770                 if (clone_flags & CLONE_VFORK)
1771                         trace = PTRACE_EVENT_VFORK;
1772                 else if ((clone_flags & CSIGNAL) != SIGCHLD)
1773                         trace = PTRACE_EVENT_CLONE;
1774                 else
1775                         trace = PTRACE_EVENT_FORK;
1776                 
1777                 if (likely(!ptrace_event_enabled(current, trace)))
1778                         trace = 0;
1779         }
1780
1781         p = copy_process(clone_flags, stack_start, stack_size,
1782                          child_tidptr, NULL, trace, tls, NUMA_NO_NODE);
```
`ptrace`是debugger和`strace`等工具都需要的一個追蹤機制，也是一個系統呼叫。追蹤別人的稱為ptracer，而被追蹤的則是ptracee。如果不是指明了不可追蹤狀態，則會有這些判斷。由於我們現在還不須考慮`ptrace`造成的影響，這裡可以先行跳過。接著透過`copy_process`將程序內容複製存放於`p`中，其內容是個長達400行的子程序，主要是因為`task_struct`中有許多無法直接複製的內容。經過這個步驟，一個子程序的核心結構就已經就緒，但還不真正開始運行。
```
...（中略）
1789                 struct pid *pid;
1790                         
1791                 trace_sched_process_fork(current, p);
1792                         
1793                 pid = get_task_pid(p, PIDTYPE_PID);
1794                 nr = pid_vnr(pid);
1795       
...
1805                 wake_up_new_task(p);
...（後略）
```
略去1791的trace系列標記不談，這裡的`pid`結構體的實際樣貌在`include/linux/pid.h`裡面：
```
 57 struct pid
 58 {      
 59         atomic_t count;
 60         unsigned int level;
 61         /* lists of tasks that use this pid */
 62         struct hlist_head tasks[PIDTYPE_MAX];
 63         struct rcu_head rcu;
 64         struct upid numbers[1];
 65 };     
```
不可誤以為是一個整數之類的東西。事實上，在這個檔案之中，也有很詳細的註解描述pid結構事實上是用來代表個別的task（包含一般認知的程序或是執行緒）、程序群組（process group）或是session等工作單位。真正的程序標號所在之處在哪裡呢？或許可以將裡面的`upid`結構也可以理解為是這樣，但其實那是另外一個與namespace有關的機制：
```
 50 struct upid {
 51         /* Try to keep pid_chain in the same cacheline as nr for find_vpid */
 52         int nr;
 53         struct pid_namespace *ns;
 54         struct hlist_node pid_chain;
 55 };
```
也許先該說明的是，早在`copy_process`的時候，就已經為`p`這個新的工作配置了他所需要的`pid`結構（事實上這是可以在不同task之間共用的），如有必要，則`number`成員在最後面可以配置不定長度的內容，從而使得巢狀pid namespace的表述可以很方便，裡面的`nr`成員就可以分別表示這個工作在不同的命名空間內的程序編號為何。我們節錄的程式碼中有還有一個`wake_up_new_task`在`kernel/sched/core.c`之中，其意義為讓生成的新的工作配置給scheduler去排程的過程，這裡會需要一些動態追蹤的技巧，之後與其他程序管理的系統呼叫一起說明。

最後探討一下`pid_vnr`這個呼叫。各位讀者如果有使用過docker之類的容器工具，應該可以很容易接受pid namespace的觀念，所以我這裡主要先給沒有這個概念的讀者一個心理建設。一個程序對於他自己的編號的認識，是由它所處的命名空間決定的。也許有一個程序被生成在一個非預設的命名空間，那麼它也許會覺得自己是編號1的拓荒者，而他的子孫程序會沿襲這個命名空間2,3,4...這樣編號下去。這些程序也可能能夠繼續展開新的PID命名空間（容器中的容器之類的使用情境）。所以這個看起來是為了取得程序編號的呼叫，實際上會考慮所處的命名空間來決定。預設所有的程序都是在同一個NS中。

在`kernel/pid.c`裡面：
```
500 pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
501 {
502         struct upid *upid; 
503         pid_t nr = 0;
504                       
505         if (pid && ns->level <= pid->level) {
506                 upid = &pid->numbers[ns->level];
507                 if (upid->ns == ns)
508                         nr = upid->nr;
509         }             
510         return nr;
511 }                     
512 EXPORT_SYMBOL_GPL(pid_nr_ns);
513                       
514 pid_t pid_vnr(struct pid *pid)                                                                                                           
515 {                     
516         return pid_nr_ns(pid, task_active_pid_ns(current));
517 }
```
514行的`pid_vnr`先取得當前程序的PID命名空間，再呼叫`pid_nr_ns`呼叫，就是要看看這個`pid`物件在該命名空間的編號。經過多個階段驗證之後，按照所在的命名空間先取得`pid`內正確的`number`陣列成員，再從對應的`upid`結構中取得編號回傳。那麼，為什麼子程序會拿到0的值？筆者這裡猜想，應該是因為在後來新工作正式開始之後，回到使用者空間之前清空了原本的eax暫存器的值，才會有這樣的行為，但這大概需要其他方法來驗證了。

---
### 案例

為了確實讓系統使用`fork`呼叫而非Linux更萬用的`clone`，必須來寫一點組合語言了，請參考下列程式碼：
```
#include<unistd.h>
#include<stdio.h>

int main(){
	pid_t pid;

	asm("mov $57, %rax\n");
	asm("syscall\n");

	asm("movl %%eax, %0\n" 
		:"=m" (pid));

	printf("I am %d and get %d from fork()\n", getpid(), pid);
}
```
其中，57是`fork`系統呼叫的編號，將之塞入rax暫存器中，然後引用系統呼叫指令。這會觸發一個中斷，經過系統呼叫進入點，來到`sys_fork`，也就是本文所著墨的地方。然後將回傳值搬到`pid`變數中，最後印出一行字說明自己的程序ID（透過`getpid`呼叫，之後或有機會提到）還有`fork`之後的回傳值為何。這個程式執行起來大概會像這樣子：
```
[noner@archvm 7-fork]$ ./a.out 
I am 1360 and get 1361 from fork()
I am 1361 and get 0 from fork()
```
讀者可以自由修改這些程式，改成呼叫C函式庫的`fork`，再用`strace`檢查的話，即可發現wrapper會引用`clone`，而上述版本使用`fork`。

---
### 結論

我們檢視了`fork`系統呼叫的內容，並靜態地觀察`fork`如何回傳給出一個值。一般來說，父程序有責任監看子程序的作為，所以會需要一個`wait`呼叫，而這也是我們明天的主題。感謝各位讀者，我們明天再會！
