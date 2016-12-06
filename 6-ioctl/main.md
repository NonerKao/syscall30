### 前情提要

前四個都是檔案操作系列系統呼叫，基本的讀寫和開關。

---
### 關於`ioctl`

有別於前四者的清楚明瞭，`ioctl`乍看之下是比較看不懂，但唸一唸也就順了，
```
NAME
       ioctl - control device

SYNOPSIS
       #include <sys/ioctl.h>

       int ioctl(int fd, unsigned long request, ...);
```
就是I/O相關的Control。有些時候對於一些特殊檔案的處理，沒有辦法透過讀和寫兩種操作來滿足，這時候如果要把諸般特殊裝置的特性做哲學上的分類，使得某些操作可以共用系統呼叫的話，實在是太過麻煩了，何況也可能根本無法分析個別的天差地遠的裝置。於是，這個通用的系統呼叫就出現了。在POSIX手冊（`ioctl(3)`）裡面有更多說明。也可以在[LWN](https://lwn.net/Articles/)參考一些[早先關於`ioctl`的討論](https://lwn.net/Articles/191653/)。

---
### 追蹤！

`ioctl`的定義在`fs/ioctl.c`，
```
685 SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
686 {       
687         int error;
688         struct fd f = fdget(fd);
689 
690         if (!f.file) 
691                 return -EBADF;
692         error = security_file_ioctl(f.file, cmd, arg);
693         if (!error)
694                 error = do_vfs_ioctl(f.file, fd, cmd, arg);
695         fdput(f);
696         return error;
697 }
```

關於`fd`相關操作，我們已經在前面提過很多，這裡就略過不提。`security_file_ioctl`和Linux安全模組（LSM）有關，而`do_vfs_ioctl`看起來似乎扮演著最重要的角色，但這裡筆者打算使用動態追蹤方式來確認這些呼叫的後續為何。

稍微修改來自[stackoverflow](http://stackoverflow.com/questions/1022957/getting-terminal-width-in-c)的範例程式碼，這個程式使用`ioctl`與已經開啟的終端機溝通，然後取得終端機大小的資訊：
```
  1 #include<sys/ioctl.h>
  2 #include <stdio.h>
  3 
  4 int main (void)
  5 {
  6         struct winsize w;
  7         ioctl(0, TIOCGWINSZ, &w);
  8 
  9         printf ("This is %dx%d\n", w.ws_row, w.ws_col);
 10         return 0;
 11 }   
```
建議讀者也能夠一起trace一次會比較了解這篇所看到的東西。

動態追蹤的過程如下：在gdb裡面的時候給`sys_ioctl`在`fd == 0`且`cmd == 0x5413`的條件中斷，之所以有這個魔術數字，是因為在debug階段無法取得`TIOCGWINSZ`這個指令的實際值，所以只好透過cscope找到`include/uapi/asm-generic/ioctls.h`中的定義。據此，後續可以發現`security_file_ioctl`在這個階段沒有觸發，筆者猜測這是因為核心的組態沒有啟用相關功能的緣故。

進入到`do_vfs_ioctl`之後，
```
618 int do_vfs_ioctl(struct file *filp, unsigned int fd, unsigned int cmd,
619              unsigned long arg)
620 {
621         int error = 0;
622         int __user *argp = (int __user *)arg;
623         struct inode *inode = file_inode(filp);
624 
625         switch (cmd) {
626         case FIOCLEX:
627                 set_close_on_exec(fd, 1);
628                 break;
629 
630         case FIONCLEX:
631                 set_close_on_exec(fd, 0);
632                 break;
...
675         default:
676                 if (S_ISREG(inode->i_mode))
677                         error = file_ioctl(filp, cmd, arg);
678                 else
679                         error = vfs_ioctl(filp, cmd, arg);
680                 break;
681         }
682         return error;
683 }
```
可以看到這裡主要會依照`cmd`的指令值來判斷接下來的程式流向。這些有列出來的旗標形式像是這種直接宣告的（`include/uapi/asm-generic/ioctls.h`）：
```
 81 #define FIONCLEX        0x5450
 82 #define FIOCLEX         0x5451
 83 #define FIOASYNC        0x5452
```
或是這種透過巨集組合的（`include/uapi/linux/fs.h`）：
```
232 #define FICLONE         _IOW(0x94, 9, int)
233 #define FICLONERANGE    _IOW(0x94, 13, struct file_clone_range)
234 #define FIDEDUPERANGE   _IOWR(0x94, 54, struct file_dedupe_range)
```

這次使用的`TIOCGWINSZ`不在其中，因此會進入675行的default判斷。裡面的`S_ISREG`是**本檔案是否為一般檔案**的意思，相關的判斷我們有機會在檔案處理系統呼叫篇的`stat`中探索。終端機因為不是一般檔案（Linux的話是`/dev/pts/`底下的一些特殊檔案），所以接下來會進入`vfs_ioctl`，然後對應到`tty_ioctl`，其中就有回傳視窗設定的判斷了：
```
（drivers/tty/tty_io.c）
...
2891         case TIOCSWINSZ:
2892                 return tiocswinsz(real_tty, p);
...
```

---
### 終端機搜秘

追根究底，這個程式的終端機檔案描述子0，是什麼時候開啟的？如果各位使用`strace`搜尋，想必看到的開頭會類似
```
[noner@archvm 6-ioctl]$ strace ./a.out 
execve("./a.out", ["./a.out"], [/* 19 vars */]) = 0
brk(NULL)                               = 0x15a2000
access("/etc/ld.so.preload", R_OK)      = -1 ENOENT (No such file or directory)
open("/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
...
```
第一次出現的`open`呼叫就已經為我們回傳3了！也就是說，其實0,1,2是這個程式的parent在fork之後給予它的檔案描述子。這個parent是筆者所在的bash，確認一下：
```
[noner@archvm 6-ioctl]$ ps
  PID TTY          TIME CMD
 1365 pts/0    00:00:00 bash
 1409 pts/0    00:00:00 ps
[noner@archvm 6-ioctl]$ ls -al /proc/1365/fd 
total 0
dr-x------ 2 noner noner  0 Dec  6 19:46 .
dr-xr-xr-x 9 noner noner  0 Dec  6 19:46 ..
lrwx------ 1 noner noner 64 Dec  6 19:46 0 -> /dev/pts/0
lrwx------ 1 noner noner 64 Dec  6 19:46 1 -> /dev/pts/0
lrwx------ 1 noner noner 64 Dec  6 19:46 2 -> /dev/pts/0
lrwx------ 1 noner noner 64 Dec  6 19:46 255 -> /dev/pts/0
```
但，我們該如何找到終端機被開啟的瞬間呢？是否存在bash的啟動時呢？為了印證這點，使用`strace`追蹤bash的啟動：
```
[root@archvm ~]# strace -f /bin/bash 2>&1 | grep open
open("/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
open("/usr/lib/libreadline.so.7", O_RDONLY|O_CLOEXEC) = 3
open("/usr/lib/libdl.so.2", O_RDONLY|O_CLOEXEC) = 3
open("/usr/lib/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
open("/usr/lib/libncursesw.so.6", O_RDONLY|O_CLOEXEC) = 3
open("/dev/tty", O_RDWR|O_NONBLOCK)     = 3
exit
[root@archvm ~]#
```
連bash都不是的話，那麼就該更往上層追溯：
```
[root@archvm ~]# ps auxf
...
root      1285  0.0  0.6 100160  7048 ?        Ss   19:38   0:00  \_ sshd: root@pts/0
root      1348  0.0  0.3  14180  3224 pts/0    Ss   19:38   0:00      \_ -bash
root      1458  0.0  0.2  32984  2696 pts/0    R+   19:58   0:00          \_ ps auxf
```
也許是因為在已經開啟終端機的bash裡開啟的話會有一些不一樣的行為，那麼就該來看看登入shell的bash如何運作。然而，我們無法strace一個登入shell，所以應該做的事情就是對sshd做strace，然後在sshd創造子程序的時候，一併監控子程序的行為。所以就直接這麼做吧：
```
[root@archvm ~]# strace -ff /sbin/sshd -D -p 5566 2>&1 | grep open
...
[pid  2485] open("/dev/ptmx", O_RDWR)   = 7
[pid  2485] open("/etc/group", O_RDONLY|O_CLOEXEC) = 8
[pid  2485] open("/dev/pts/1", O_RDWR|O_NOCTTY) = 8
[pid  2485] open("/etc/group", O_RDONLY|O_CLOEXEC) = 9
[pid  2487] open("/dev/tty", O_RDWR|O_NOCTTY <unfinished ...>
[pid  2487] <... open resumed> )        = -1 ENXIO (No such device or address)
[pid  2487] open("/dev/tty", O_RDWR|O_NOCTTY) = -1 ENXIO (No such device or address)
[pid  2487] open("/dev/pts/1", O_RDWR <unfinished ...>
[pid  2487] <... open resumed> )        = 4
[pid  2485] open("/dev/ptmx", O_RDWR)   = 7
[pid  2485] open("/etc/group", O_RDONLY|O_CLOEXEC) = 8
[pid  2485] open("/dev/pts/1", O_RDWR|O_NOCTTY) = 8
[pid  2485] open("/etc/group", O_RDONLY|O_CLOEXEC) = 9
[pid  2487] open("/dev/tty", O_RDWR|O_NOCTTY <unfinished ...>
[pid  2487] <... open resumed> )        = -1 ENXIO (No such device or address)
[pid  2487] open("/dev/tty", O_RDWR|O_NOCTTY) = -1 ENXIO (No such device or address)
[pid  2487] open("/dev/pts/1", O_RDWR <unfinished ...>
[pid  2487] <... open resumed> )        = 4
```
其中，`-ff`是`strace`為了觀察child的行為而下的參數，因此輸出訊息有別於主程序，前方加上程序ID的前綴。至於`sshd`的參數，`-D`是使它不要背景化，`-p 5566`則是指定可以連線的port。因為預先在系統內由systemd管理的sshd無法透過這種方式監控，這裡就需要自己開一個。特別要注意的是`/dev/ptmx`和`/dev/pts`的開啟，因為Linux管理虛擬終端機的方法透過`ptmx`分配。至於這些ID，透過`ps`追蹤：
```
root      2483  0.0  0.4  40400  4464 pts/0    S+   21:28   0:00          |   \_ /sbin/sshd -D -p 5566
root      2485  0.0  0.6 100156  6796 ?        Ss   21:28   0:00          |       \_ sshd: root@pts/1
root      2487  0.0  0.3  14180  3276 pts/1    Ss   21:28   0:00          |           \_ -bash
root      2491  0.0  0.2  32984  2752 pts/1    R+   21:28   0:00          |               \_ ps auxf
```
開啟檔案狀況可以從/proc/2485/fd觀察得，
```
[root@archvm ~]# ls -al /proc/2485/fd（註：這是sshd: root@pts/1）
total 0
dr-x------ 2 root root  0 Dec  6 21:28 .
dr-xr-xr-x 9 root root  0 Dec  6 21:28 ..
lrwx------ 1 root root 64 Dec  6 21:28 0 -> /dev/null
lrwx------ 1 root root 64 Dec  6 21:28 1 -> /dev/null
lrwx------ 1 root root 64 Dec  6 21:35 10 -> /dev/ptmx
lrwx------ 1 root root 64 Dec  6 21:28 2 -> /dev/null
lrwx------ 1 root root 64 Dec  6 21:28 3 -> 'socket:[18634]'
lrwx------ 1 root root 64 Dec  6 21:28 4 -> 'socket:[17744]'
lr-x------ 1 root root 64 Dec  6 21:28 5 -> 'pipe:[17746]'
l-wx------ 1 root root 64 Dec  6 21:35 6 -> 'pipe:[17746]'
lrwx------ 1 root root 64 Dec  6 21:35 7 -> /dev/ptmx
lrwx------ 1 root root 64 Dec  6 21:35 9 -> /dev/ptmx
[root@archvm ~]# ls -al /proc/2487/fd（註：這是-bash）
total 0
dr-x------ 2 root root  0 Dec  6 21:28 .
dr-xr-xr-x 9 root root  0 Dec  6 21:28 ..
lrwx------ 1 root root 64 Dec  6 21:28 0 -> /dev/pts/1
lrwx------ 1 root root 64 Dec  6 21:28 1 -> /dev/pts/1
lrwx------ 1 root root 64 Dec  6 21:28 2 -> /dev/pts/1
lrwx------ 1 root root 64 Dec  6 21:35 255 -> /dev/pts/1
```

合理懷疑是由sshd的父程序開啟之後，再於-bash程序中使用`dup2`之類的系統呼叫將已開啟的虛擬終端機pts對應到0,1,2，這可以透過類似的手法驗證。`dup`系列的系統呼叫我們之後有機會的話可以再來介紹。也推薦各位參考[這篇](http://blog.csdn.net/u011279649/article/details/9833613)文章，從另外一個面向觀察這件事情。

---
### 結論

`ioctl`對於一般檔案和特殊檔案都提供許多除了讀寫之外的操作，我們這次除了簡單看了一下終端機的一個`ioctl`指令之外，也大致看到了C語言標準輸入輸出的啟動如何與虛擬終端機的特殊檔案開啟扯上關係。明天開始我們將進入程序管理的系統呼叫篇章，明日再會！
