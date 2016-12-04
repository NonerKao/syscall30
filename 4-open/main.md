### 前情提要

我們在前兩天分別以終端機上的標準輸入輸出作為`write`和`read`的範例說明，從系統呼叫本體追蹤到虛擬檔案系統層（`vfs_xxx`），再到終端機專屬的`tty_xxx`函式，再下一層到描述終端機line discipline的部份打住。過程中，無論標準輸入輸出看起來再如何不像檔案操作，我們都可以發現在抽象的意涵以及核心的實作上，終端機都透過某些機制被視為開啟的檔案被讀寫。這是如何作到的？且看今日的`open`系統呼叫。

---
### 開啟

按照慣例來看看這個玩意的標準定義，想必已經成為系列讀者的直覺了。但是這次可以明顯的發現，POSIX版本和Linux版本的手冊有些可以一眼看出的差異。先看POSIX版本：
```
NAME
       open, openat — open file relative to directory file descriptor

SYNOPSIS
       #include <sys/stat.h>
       #include <fcntl.h>

       int open(const char *path, int oflag, ...);
       int openat(int fd, const char *path, int oflag, ...);
```
這則是Linux版本：
```
NAME
       open, openat, creat - open and possibly create a file

SYNOPSIS
       #include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>

       int open(const char *pathname, int flags);
       int open(const char *pathname, int flags, mode_t mode);

       int creat(const char *pathname, mode_t mode);

       int openat(int dirfd, const char *pathname, int flags);
       int openat(int dirfd, const char *pathname, int flags, mode_t mode);

   Feature Test Macro Requirements for glibc (see feature_test_macros(7)):
   ...
```
Linux給的API有兩種open，分別取得兩個或三個參數；而POSIX標準則給了一個`...`。如果沒有詳閱手冊內容（如同筆者一樣），會沒有辦法看出其間的奧妙所在。其實兩者都把可能出現的第三個參數存在的理由放置在`O_CREAT`的說明中，因為開啟檔案的時候會需要設定這個檔案的屬性，也就是當使用者執行指令
```
$ ls -l
```
的時候，會看見的那一串存取控制清單（Access Control List）。說來慚愧，筆者是先在freebsd的`open(2)`手冊中找到的，因為它們的SYNOPSIS後緊接著的DESCRIPTION開宗明義就說明了額外參數的需要情境；反回來對照這兩者，才看出端倪。也就是說，其實Linux版本的手冊，也就只是把那種例外狀況列出來而已。

使用者空間如何使用到`open`呢？其實就在標準函式庫的`fopen`之中，比方說也會一起上傳到[github](https://github.com/NonerKao/syscall30)的這份程式碼：
```
#include<stdio.h>

int main(){
	FILE *fp[6];

	fp[0] = fopen("/tmp/r.txt", "r");
	fp[1] = fopen("/tmp/r+.txt", "r+");
	fp[2] = fopen("/tmp/w.txt", "w");
	fp[3] = fopen("/tmp/w+.txt", "w+");
	fp[4] = fopen("/tmp/a.txt", "a");
	fp[5] = fopen("/tmp/a+.txt", "a+");

	fclose(fp[0]);
	fclose(fp[1]);
	fclose(fp[2]);
	fclose(fp[3]);
	fclose(fp[4]);
	fclose(fp[5]);
	return 0;
}
```
使用前請記得先創好`/tmp/r*.txt`這兩個文件，否則在fclose的時候會出問題。使用`strace`觀察這個程式的運行，就會看到本日主角`open`在其中扮演的角色，
```
...
open("/tmp/r.txt", O_RDONLY)            = 3
open("/tmp/r+.txt", O_RDWR)             = 4
open("/tmp/w.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 5
open("/tmp/w+.txt", O_RDWR|O_CREAT|O_TRUNC, 0666) = 6
open("/tmp/a.txt", O_WRONLY|O_CREAT|O_APPEND, 0666) = 7
lseek(7, 0, SEEK_END)                   = 0
open("/tmp/a+.txt", O_RDWR|O_CREAT|O_APPEND, 0666) = 8
close(3)                                = 0
close(4)                                = 0
close(5)                                = 0
close(6)                                = 0
close(7)                                = 0
close(8)                                = 0
...
```
對比於`fopen(2)`手冊中的各個flag說明的話：
```
       r      Open text file for reading.  The stream is positioned at the beginning of the file.

       r+     Open for reading and writing.  The stream is positioned at the beginning of the file.

       w      Truncate file to zero length or create text file for writing.  The stream is positioned at the  beginning  of
              the file.

       w+     Open  for  reading  and  writing.   The file is created if it does not exist, otherwise it is truncated.  The
              stream is positioned at the beginning of the file.

       a      Open for appending (writing at end of file).  The file is created if it does not exist.  The stream is  posi‐
              tioned at the end of the file.

       a+     Open for reading and appending (writing at end of file).  The file is created if it does not exist.  The ini‐
              tial file position for reading is at the beginning of the file, but output is always appended to the  end  of
              the file.
```
就會發現這些`O_*`的flag意義一目了然了。

---
### 靜態追蹤

`open`系統呼叫位於`fs/open.c`之中，
```
1049 SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
1050 {
1051         if (force_o_largefile())
1052                 flags |= O_LARGEFILE;
1053 
1054         return do_sys_open(AT_FDCWD, filename, flags, mode);
1055 }
1056 
1057 SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags,
1058                 umode_t, mode)
1059 {
1060         if (force_o_largefile())
1061                 flags |= O_LARGEFILE;
1062 
1063         return do_sys_open(dfd, filename, flags, mode);
1064 }
```
一開始的是否強制開啟為大檔案的判斷，進去看了一下發現目前的判斷僅有一些CPU架構的32或64bit的區分，`do_sys_open`才是核心的部份。之所以有`do_sys_open`這樣的設計，是因為`open`與`openat`（甚至還有為了歷史相容性而維護的`creat`）都使用類似的功能，能夠共用的底層程式碼佔據了大部分的緣故。為了理解`AT_FDCWD`這個參數，我們多引入了`openat`的實作部份作為參考。

`open`可以很容易的理解為給定路徑名稱、開啟檔案模式、以及存取權限，回傳一個代表該檔案的檔案描述子的過程。其中給定的路徑如果是絕對的，那麼要存取哪個檔案對核心來說是很明確的；若是相對路徑，則要有一個相對的參考點。在`open`的情況，這個參考點就是當前目錄，`openat`的功能則是讓使用者可以選擇傳入參考點的檔案描述子。在這個階段，兩個系統呼叫只有一個參數的差異的原因也就不難理解了。

所以就看看`do_sys_open`吧！比起以往所見的函數，這個算是有點份量的，於是筆者在這裡先行拆分，這是第一個部份：
```
1021 long do_sys_open(int dfd, const char __user *filename, int flags, umode_t mode)
1022 {
1023         struct open_flags op;
1024         int fd = build_open_flags(flags, mode, &op);
1025         struct filename *tmp;
1026 
1027         if (fd)
1028                 return fd;
```
筆者在這第一段有點不太舒服的感覺，與過去trace其他部份的時候的美麗與和諧感有些出入。主要是因為`fd`這個變數的宣告，顯然是為了之後要回傳一個可用的檔案描述子而設的存放空間，然而這裡的`build_open_flags`，也就是把`flags`或是可能會需要的權限模式`mode`打包成一個`struct open_flags`結構。既然已經打包在`op`變數，那麼為什麼在這時候回傳給fd呢？搭配1027等兩行的描述，**如果fd有東西則回傳**，乍看之下令人以為這裡有一個捷徑可以根據檔名查找以存在的檔案描述子，但**事實上只是將這個變數拿來兼用，回傳可能出現的錯誤訊息，以及正常則繼續的意義。**仍然是可以接受啦，因為仔細想想，如果要給一個變數給這個打包開啟flag的過程，真的也是蠻浪費的。

但還是有令人不爽的地方，比方說`op`一直都是operations的慣例縮寫，這裡也許`opf`比較妥當吧？還有必須小心的是，`tmp`變數的型別是`struct filename`的指標，這和傳入的使用者空間字串`filename`是不同的東西。
```
1029 
1030         tmp = getname(filename);
1031         if (IS_ERR(tmp))
1032                 return PTR_ERR(tmp);
1033 
......
1045         putname(tmp);
1046         return fd;
1047 }
```
筆者將中間部份先行挖空，突顯出這個函數頭尾的get/put結構。`getname`必須要將使用者空間字串變化為一個核心空間的`filename`結構，詳細過程在`fs/namei.c`裡面的`getname_flags`函數，這裡就簡單描述一下。首先透過[audit系統](https://wiki.archlinux.org/index.php/Audit_framework)的輔助，有機會能夠存取先前的紀錄而快速根據檔名取得一個`struct filename`物件。若是沒有這個捷徑可走，則老實的配置記憶體、複製檔名字串，並將這個物件的存取數設為一。期間當然有許多錯誤判斷如檔名過長之類的。

`putname`是個相對的呼叫，裡面有一個[最近讓Linus Torvalds抓狂](https://linux.slashdot.org/story/16/10/05/210227/linus-torvalds-says-buggy-crap-made-it-into-linux-48)的`BUG_ON`，設定在存取數小於等於零的狀況。存取數減一之後若仍大於零，則直接回傳。最後剩下的是存取數為零的情況，這時候就該把傳入的結構free掉了。
```
1034         fd = get_unused_fd_flags(flags);
1035         if (fd >= 0) {
1036                 struct file *f = do_filp_open(dfd, tmp, &op);
1037                 if (IS_ERR(f)) {
1038                         put_unused_fd(fd);
1039                         fd = PTR_ERR(f);
1040                 } else {
1041                         fsnotify_open(f);
1042                         fd_install(fd, f);
1043                 }
1044         }
```
在這之間是真正把開啟的檔案對應到檔案描述子的過程。首先透過`get_unused_fd_flags`取得未使用的`fd`，正如前段顯示的strace片段一般，通常`open`的結果就是從3開始依序增加，因為0~2都有標準介面使用了。這個函數在`fs/file.c`之中，
```
560 int get_unused_fd_flags(unsigned flags)                                                                                                   
561 {             
562         return __alloc_fd(current->files, 0, rlimit(RLIMIT_NOFILE), flags);
563 }             
```
緊接著呼叫的是某個雙底線開頭的內部介面，這樣的模式我們已經看過很多次了，通常這是在提示我們，這個功能有些內部的構造可以為多個介面共享，因而是個更基本的函數。`__alloc_fd`的註簡潔地解說這是一個**配置一個檔案描述子並設之為忙碌**的函數，傳入的參數有昨天見過的`current->files`，也就是一個程序的開啟檔案狀態；第二個及第三個參數代表的是從0開始、至`RLIMIT_NOFILE`（可開啟檔案上限）結束，想必有用過使用者空間的rlimit指令的讀者對這個概念並不陌生；第四個參數也是照樣傳入。判別是否有**單一程序開啟檔案過多**的錯誤回傳也是在這個部份完成的。

接下來是`do_flip_open`，前兩個參數可以組合成從根目錄開始的絕對路徑，確保一定能夠存取到這個檔案；`op`則是之前組合好的，用來代表使用者想要開啟該檔案的狀態。若是成功的話會進入`else`的部份，

