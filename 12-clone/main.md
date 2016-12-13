### 前情提要

前幾日介紹了與程序創建相關的`fork`、控制相關的`wait`、資訊取得/設定相關的`xetxid`系列呼叫等等，程序管理功能已經接近尾聲。然而，Linux核心之中的`struct task_struct`其實是對程序與執行緒一體適用的，我們目前為止則還沒看到執行緒部份的面向。本日的主題就是要介紹`clone`，這個Linux核心實作的萬用介面，如今扮演的角色不僅僅是創建傳統的程序和輕量的執行緒，還有現在各大廠商都紛紛引用的容器技術。

---
### 範例
```
  1 #include <stdio.h>
  2 #include <pthread.h>
  3 #include <sys/resource.h>
  4 #include <sys/wait.h>
  5 #define _GNU_SOURCE       
  6 #include <sys/syscall.h>
  7 #include <unistd.h>
  8          
  9 void *thread_fn(void* arg){
 10         int tid = syscall(SYS_gettid);                                                                                                   
 11         printf("I am the thread %d in child %d\n", tid, getpid());
 12         return NULL;
 13 }        
 14          
 15 int main(){
 16          
 17         pthread_t pt;
 18         pid_t pid;
 19         int wstatus;
 20          
 21         if( (pid = fork()) != 0 ) {
 22                 waitpid(pid, &wstatus, 0);
 23         } else {
 24                 printf("I am the child %d\n", getpid());
 25                 pthread_create(&pt, NULL, &thread_fn, NULL);
 26                 pthread_join(pt, NULL);
 27         }
 28          
 29 }        
```
這個範例程式先使用`fork` API呼叫（這將會轉化成`clone`，我們在`fork`一文曾經展示過），然後在子程序中引用pthread API創建執行緒，並在其中呼叫一個`gettid`系統呼叫。使用`strace`觀察，可以發現：
```
[noner@heros 12-clone]$ strace -f ./a.out 
execve("./a.out", ["./a.out"], [/* 41 vars */]) = 0
...
clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f0f004ed9d0) = 9495
...
strace: Process 9495 attached
[pid  9494] wait4(9495,  <unfinished ...>
...
[pid  9495] clone(strace: Process 9496 attached
child_stack=0x7f0effd44ff0, flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID, parent_tidptr=0x7f0effd459d0, tls=0x7f0effd45700, child_tidptr=0x7f0effd459d0) = 9496
...
```
執行起來則有這樣的輸出：
```
[noner@heros 12-clone]$ ./a.out 
I am the child 9732
I am the thread 9733 in child 9732
```

---
### `clone`

與其他文章的系統呼叫不盡相同的是，`clone`是Linux獨有的，並且glibc的wrapper和系統呼叫的介面不同。所以這次我們不從參考手冊理解這個call，而是直接進入核心的原始碼，但是一開始就遇到了瓶頸：（在`kernel/fork.c`中）
```
1866 #ifdef __ARCH_WANT_SYS_CLONE
1867 #ifdef CONFIG_CLONE_BACKWARDS
1868 SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
1869                  int __user *, parent_tidptr,
1870                  unsigned long, tls,
1871                  int __user *, child_tidptr)
1872 #elif defined(CONFIG_CLONE_BACKWARDS2)
1873 SYSCALL_DEFINE5(clone, unsigned long, newsp, unsigned long, clone_flags,
1874                  int __user *, parent_tidptr,
1875                  int __user *, child_tidptr,
1876                  unsigned long, tls)
1877 #elif defined(CONFIG_CLONE_BACKWARDS3)
1878 SYSCALL_DEFINE6(clone, unsigned long, clone_flags, unsigned long, newsp,
1879                 int, stack_size,
1880                 int __user *, parent_tidptr,
1881                 int __user *, child_tidptr,
1882                 unsigned long, tls)
1883 #else            
1884 SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
1885                  int __user *, parent_tidptr,
1886                  int __user *, child_tidptr,                                                                                          
1887                  unsigned long, tls)
1888 #endif           
1889 {                
1890         return _do_fork(clone_flags, newsp, 0, parent_tidptr, child_tidptr, tls);
1891 }                
1892 #endif           
```
到底是哪個介面呢？有三種向後相容的模式，還有最後一個`else`。究竟是何者？運用筆者許久沒有提到的動態追蹤，將中斷設在`sys_clone`的系統呼叫函數，再將前一節的範例帶入執行，立刻可以看到是最後一個，也就是非那些向後相容的版本。實際上如何，還是要看各位讀者作實驗的平台而定，筆者得到這個結果，只是說明筆者並未啟用任何`CONFIG_CLONE_BACKWARDS*`的選項。

總之，接下來就將傳入參數帶入`_do_fork`，相關的內容與[第七日的`fork`](http://ithelp.ithome.com.tw/articles/10185342)比較得：
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
由此我們可得知
- 純粹的`fork`呼叫（我們在`fork`的範例程式使用）會轉化為最陽春的`_do_fork`，僅有地 一個的`flags`參數傳入`SIGCHLD`。
- API的`fork`呼叫，根據上面的`strace`訊息得到`clone`的會是`child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f0f004ed9d0`
- API的`pthread_create`呼叫，會得到`child_stack=0x7f0effd44ff0, flags=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID, parent_tidptr=0x7f0effd459d0, tls=0x7f0effd45700, child_tidptr=0x7f0effd459d0)`

其中可見，thread的創建情況會傳入給予child使用的stack空間，而fork程序則不需要；`CLONE_THREAD`的突特性也宣告了為了執行緒而非程序產生新的工作，儘管他們在核心的角度看起來是很相像的。由於`_do_fork`的閱讀大多與`fork`重疊，而這些旗標的符號又可以在clone(2)手冊中找到定義，因此這個部份就先略過。


---
### 結論

`clone`相比於`fork`是個較萬用的接口，也展示了核心的彈性。明天我們將介紹程序的結束呼叫`exit`並開啟新篇章，我們明天再會！
