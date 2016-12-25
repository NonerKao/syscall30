### 前情提要

記憶體管理篇告一段落，接下來在網路相關的系統呼叫之前，插播一個跨行程通訊用的呼叫：`pipe`（管線）。

---
### 

管線最常見的使用方式就是在shell環境之下使用'|'符號連接不同指令。這麼做的話，會使得左端的指令的標準輸出接到右端的指令的標準輸入。手冊是這麼寫的：
```
NAME
       pipe, pipe2 - create pipe

SYNOPSIS
       #include <unistd.h>

       int pipe(int pipefd[2]);

       #define _GNU_SOURCE             /* See feature_test_macr
os(7) */
       #include <fcntl.h>              /* Obtain O_* constant d
efinitions */
       #include <unistd.h>

       int pipe2(int pipefd[2], int flags);
```
說明手冊也包含了GNU的特殊feature的`pipe2`，這裡就不提而專注在`pipe`呼叫上面。乍看之下很簡陋，只配置兩個檔案描述子，但其實這正是管線奧妙之處。

成功回傳之後，會有一個唯讀的`pipefd[0]`和唯寫的`pipefd[1]`共兩個檔案描述子。雖然本系列的主題是系統呼叫，但是這裡要多花一點篇幅描述一下bash如何使用的例子。bash會有很多部份在解析命令列的語法，而當他開始打算執行命令列的時候會用`execute_command`這個函數，隨後呼叫一個`execute_command_internal`（在`execute_cmd.c`），這個函數預設會有`pipe_in = NO_PIPE; pipe_out = NO_PIPE`的參數輸入。發現有'|'符號涉入的時候，就會呼叫內含`fork`的`make_child`函數（在`jobs.c`）；創得兩個子程序之後，就會使用`pipe`系統呼叫先建立一個通道，然後在前一個程序關掉唯讀`[0]`的接口，後一個程序則關掉唯寫`[1]`的接口，如此一來左程序就可以透過這個管線傳資訊給右程序。

> 其實還有一個重點是，左程序的`pipefd[1]`必須對應到標準輸出的1，而右程序的`pipefd[0]`則必須對應到標準輸入的0。這個功能使用`dup*`系列的系統呼叫實現。系列文已經沒有空間可以介紹這個系統呼叫，這裡簡單描述，就是複製一個檔案述子的所有功能給另外一個檔案描述子。如果被複製的目標原先存在，則直接關閉。以管線的應用例子，就是左程序將`pipefd[1]的性質給予`標準輸出的1，而原本開啟終端機的1號描述子會被關掉，右程序方向相反以此類推。

---
### 範例程式

主程式的流程是先用`pipe`創造一個管線，然後分別`fork`出兩個子程序之後，在主程式這一端關閉管線的兩端，然後進入等待狀態，於子程序離開時顯示幾號子程序離開的訊息。
```
 40 int main(){
 41         int wstatus;
 42         int fd[2];
 43 
 44         FILE *target;
 45 
 46         int ret;
 47         SYSCALL_ERROR(ret, pipe, fd);
 48 
 49         pid_t pid;
 50         SYSCALL_ERROR(pid, fork);
 51         if(pid == 0)
 52                 return wchild(fd);
 53 
 54         SYSCALL_ERROR(pid, fork);
 55         if(pid == 0)
 56                 return rchild(fd);
 57 
 58         SYSCALL_ERROR(ret, close, fd[1]);
 59         SYSCALL_ERROR(ret, close, fd[0]);
 60 
 61         int count = 2;
 62         while(count){
 63                 siginfo_t sig;
 64                 waitid(P_ALL, -1, &sig, WEXITED);
 65                 printf("The child %d exits.\n", sig.si_pid);
 66                 count--;
 67         }
 68 
 69         return 0;
 70 }
```
其中`SYSCALL_ERROR`是個附帶錯誤處理的巨集，立刻來看看兩個子程序分別在做什麼：
```
  9 #define SYSCALL_ERROR(ret, call, ...)\
 10         ret = call(__VA_ARGS__);\
 11         if(ret == -1){\
 12                 fprintf(stderr, "%s error at %d\n", #call, __LINE__);\
 13                 exit(-1);\
 14         }
 15 
 16 const char *msgo = "Hi, this is from %d through stdout.\n";
 17 const char *msge = "Hi, this is from %d through stderr.\n";
 18 
 19 int wchild(int fd[]){
 20         int ret;
 21         SYSCALL_ERROR(ret, close, fd[0]);
 22         SYSCALL_ERROR(ret, dup2, fd[1], 1);
 23 
 24         printf(msgo, getpid());
 25         fprintf(stderr, msge, getpid());
 26         return 0;
 27 }
 28 
 29 int rchild(int fd[]){
 30         int ret;
 31         SYSCALL_ERROR(ret, close, fd[1]);
 32         SYSCALL_ERROR(ret, dup2, fd[0], 0);
 33 
 34         int wpid;
 35         scanf(msgo, &wpid);
 36         printf(msgo, wpid+1);
 37         return 0;
 38 }
```
負責寫的子程序會先關閉閱讀端，然後複製`fd[1]`給標準輸出用的1號。因為C函式庫的stdin結構對應到1號檔案描述子，所以這後續的`printf`就會寫到管線的寫入端，同時也寫出一個標準錯誤的。唯讀的子程序進行類似的步驟，從`scanf`取得唯寫子程序的ID（因為它傳了msgo格式附帶自己的ID），然後印出這個加一之後的值。理論上當然有可能有其他的程序在這兩個程序執行的時候生成，但是這裡姑且這麼做，反正並不是一個繁忙的主機。巨集的使用蠻直覺的，也秀了一下不定參數巨集的寫法，可以以後參考用。

執行結果可參考：
```
[demo@linux 25-pipe]$ ./a.out 
Hi, this is from 5709 through stderr.
The child 5709 exits.
Hi, this is from 5710 through stdout.
The child 5710 exits.
[demo@linux 25-pipe]$
```

---
### 追蹤

`pipe`的程式碼在`fs/pipe.c`之中，
```
 855 SYSCALL_DEFINE1(pipe, int __user *, fildes)
 856 {
 857         return sys_pipe2(fildes, 0);
 858 }
```
直接轉了一手呼叫`pipe2`，須知`pipe2`除了吃一個雙頭的檔案描述子當參數之外，還另外索取一個檔案描述子性質的參數，這裡則是傳入了0：
```
 833 SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags)
 834 {
 835         struct file *files[2];
 836         int fd[2];
 837         int error;
 838 
 839         error = __do_pipe_flags(fd, files, flags);
 840         if (!error) {
 841                 if (unlikely(copy_to_user(fildes, fd, sizeof(fd)))) {
 842                         fput(files[0]);
 843                         fput(files[1]);
 844                         put_unused_fd(fd[0]);
 845                         put_unused_fd(fd[1]);
 846                         error = -EFAULT;
 847                 } else {
 848                         fd_install(fd[0], files[0]);
 849                         fd_install(fd[1], files[1]);
 850                 }
 851         }
 852         return error;
 853 }
```
首先透過`__do_pipe_flags`取得`fd`的內容。值得注意的是這裡的`fd`是在核心的記憶體空間。之後如果發生錯誤，則直接到852行回傳該錯誤；如果沒有錯誤的情況會進入841~850的區塊之內。其中，如果複製`fd`整數陣列的過程出錯，則檔案取消存取數、當前程序取消使用這組檔案描述子的過程分別由`fput`和`put_unused_fd`完成；如果複製成功，則執行`fd_install`註冊這些取得的數字到對應的、廣義的檔案結構。

`__do_pipe_flags`的內容是：
```
 783 static int __do_pipe_flags(int *fd, struct file **files, int flags)
 784 {
 785         int error;
 786         int fdw, fdr;
 787 
 788         if (flags & ~(O_CLOEXEC | O_NONBLOCK | O_DIRECT))
 789                 return -EINVAL;
 790 
 791         error = create_pipe_files(files, flags);
 792         if (error)
 793                 return error;
```
788行的判斷是來自`pipe2`的支援只有這三種：`O_CLOEXEC`是**程序發起execve執行其他程式時關閉這個管線**；`O_NONBLOCK`是**對檔案描述子的操作不會卡住**；`O_DIRECT`則是**封包模式而非串流模式**。791行要創造能夠對接的`files`結構，然後讓他們可以互接並且一唯讀一唯寫。這個`create_pipe_files`先配置一個對應到這個管線的inode，然後透過`alloc_file`開啟兩個檔案，分別初始化、設好權限，當然也少不了檔案系統的一些處理（否則/proc/裡面就看不到了），然後就可以回傳了。`pipe`能起到作用的原因在以下節錄片段：
```
 730 int create_pipe_files(struct file **res, int flags)
 731 {
...
 755         f->f_flags = O_WRONLY | (flags & (O_NONBLOCK | O_DIRECT));
 756         f->private_data = inode->i_pipe;
...
 765         res[0]->private_data = inode->i_pipe;
 766         res[0]->f_flags = O_RDONLY | (flags & O_NONBLOCK);
 767         res[1] = f;
```
也就是他們共用同一個`inode`作為他們的`private_data`。

回到原本的`__do_pipe_flags`，
```
 795         error = get_unused_fd_flags(flags);
 796         if (error < 0)
 797                 goto err_read_pipe;
 798         fdr = error;
 799 
 800         error = get_unused_fd_flags(flags);
 801         if (error < 0)
 802                 goto err_fdr;
 803         fdw = error;
```
為管線的兩端分別取得`fdr`、`fdw`兩個檔案描述子，
```
 805         audit_fd_pair(fdr, fdw);
 806         fd[0] = fdr;
 807         fd[1] = fdw;
 808         return 0;
```
從這裡就可以回傳到`pipe2`去，並在之後執行`copy_to_user`等一系列操作完成管線的架設。

---
### 結論

本篇描述了`pipe`的機制，並且在範例程式中也順便展示了`dup2`的用法，這兩者結合起來才是在shell環境中使用'|'符號連接指令所體驗到的管線。檢視這些機制的同時，也讓我們多看到了一些檔案描述子在核心中的使用方式。如果各位讀者邦友對於檔案描述子特別有興趣，那麼一定要看`fcntl`這個系統呼叫，因為它能夠指定檔案描述子的行為。

接下來到鐵人賽結束之前，會進入連續的網路系統呼叫篇章！雖然割捨掉檔案相關的系統呼叫很難過，但是畢竟時間緊迫，筆者很遺憾的必須和`fstat`、`access`、`getdent`等`ls`指令會用到的系統呼叫說再會了，各位讀者若是有興趣的話一定也能夠自己探索看看！

筆者預計從明日開始，以一個簡單的TCP範例，介紹最簡單的socket程式如何透過系統呼叫完成作業，又這些系統呼叫在核心中又準備了哪些資料結構做了什麼事情。各位讀者，我們明天再會！
