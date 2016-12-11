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

與其他文章的系統呼叫不盡相同的是，`clone`是Linux獨有的，並且glibc的wrapper和系統呼叫的介面不同。所以這次我們不從參考手冊理解
