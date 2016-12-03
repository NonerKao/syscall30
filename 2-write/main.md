### 前情提要

前篇我們分析了一個很單純的系統呼叫：uname，就算算上筆者跳過的read lock等同步機制，裡面涉及的概念也不多，比較複雜的反而是與uname本身沒有直接相關的namespace子系統。

如果是有過trace核心程式碼經驗的讀者，想必會覺得前篇相當基本；毫無經驗讀者則可能完全看不懂。以技術文章來說，這樣的讀後感當然是最不好的。筆者於此也仍在學習，希望沒有Linux核心經驗的讀者若是對此有興趣卻又覺得文章本身教人霧裡看花，請務必留言討論，同時當然也請前輩不吝指教過於生澀的部份。

---
### 本日主題：write

Unix有許多設計哲學讓後代的作業系統自主地遵循，其中之一讓人琅琅上口的即是**Everything is a file**。然而，檔案這樣的抽象物件，若沒有能夠定義其上的開啟、創建、讀、寫等操作，身為作業系統令許多內部物件、介面、資訊成為檔案的意義也就蕩然無存。在這個意義下，寫入，`write`，當然也就是一個非常重要的系統呼叫了。

另外一個選擇`write`作為前鋒級系統呼叫來介紹的原因則是，這絕對是每一個初學者在每一個初學時分都會使用的系統呼叫，因為我們`Hello World`過。筆者原本想在這裡引用Jserv大的深入淺出Hello World系列第三章，因為那是比較接近核心的部份，但是現在暫時找不到資源，日後若有機會再行補上。

在`Hello World`的時候，無論是使用`printf`或是`puts`這樣的函數，最後都會導到libc的`write()`函數去，隨後這個函數會引發一個interrupt，x86_64架構上可以輕易的透過尋找`syscall`組語指令找到，而`write`所對應到的呼叫慣例（[Calling Convention](https://en.wikipedia.org/wiki/Calling_convention)）是設定rax為1。讀者若有興趣的話，可以自行trace libc的`printf`或`puts`實作，這些標準函式庫的呼叫最終將會導到`__write_nocancel`這個呼叫，其中便含有：
```
00000000000db529 <__write_nocancel>:
   db529:       b8 01 00 00 00          mov    $0x1,%eax
   db52e:       0f 05                   syscall
   ...
```
這樣的片段。至於為什麼這裡只設定了`eax`而沒有`write`需要的其他參數，這是因為作為系統呼叫的wrapper，libc的輸入輸出處理了許多大大小小的雜事，從`printf`到這裡（也就是user space的最後一站）的過程中解決了。另外，如果讀者好奇系統呼叫的ABI確切為何，[這裡](http://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/)有一份x86_64的參考資料。

參照手冊，可以知道今天主題`write(2)`的prototype：
```
NAME
       write - write to a file descriptor

SYNOPSIS
       #include <unistd.h>

       ssize_t write(int fd, const void *buf, size_t count);
...
```
由此，平常的C語言使用者可以很容易想像這就是`printf`或`fprintf`的終點站。這兩個不定參數函數透過給訂的格式變數（`%d`, `%f`之類）的方法帶入所有參數之後，展開在一段新的記憶體`buf`之中，然後可以計算這一段展開字串的大小當作`count`參數傳入。至於`fd`意指為何，等到之後介紹`open`的時候想必會更有發揮的空間，目前讀者需要的是一些慣例的知識，例如標準輸出的File Descriptor就會對應到1這個數字、標準輸入則是0，標準錯誤輸出是2。

在核心原始碼的位置則在`fs/read_write.c`之中：
```
 599 SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
 600                 size_t, count)
 601 {
 602         struct fd f = fdget_pos(fd);
 603         ssize_t ret = -EBADF;
 604  
 605         if (f.file) {
 606                 loff_t pos = file_pos_read(f.file);
 607                 ret = vfs_write(f.file, buf, count, &pos);
 608                 if (ret >= 0)
 609                         file_pos_write(f.file, pos);
 610                 fdput_pos(f);
 611         }
 612  
 613         return ret;
 614 }
```

---
### 靜態追蹤

從最上層看這個函數，不免令人驚嘆這些千錘百鍊的程式碼如此乾淨漂亮，寫入的抽象動作在這個層級就只有它所應具備的抽象意義。但我們仍然應該試圖深入這背後的實作為何，尤其筆者開啟這個系列，本意就是為了學習而寫；這程式碼如此易讀乃是核心開發者的功勞，不應止步於此。

傳入的`fd`只是一介整數，如果我們心中假設有個Hello World程式，那麼這裡應該是1，因為是對於該程序的標準輸出。輸出一個字串總是該要到達某個物件之中，並且改變其狀態才是。可以確定的是，如果目標只是一個整數的話，我們是無從改變它的什麼狀態的；再者，如果開啟多個不同的終端機使其各自印出任意資訊到標準輸出，這些標準輸出的`fd`都是1，怎麼不會有所衝突呢？所以我們可以預想，作業系統應該有一個內部機制，對於每一個程序，能夠將整數fd對應到期實際對應到的物件上，不管是檔案、終端機的介面或是socket（這個請期待後續）。

`fdget_pos`就扮演著這樣的角色，將整數轉換為一個`struct fd`的物件，在`include/linux/file.h`中，
```
 29 struct fd {
 30         struct file *file;
 31         unsigned int flags;
 32 };
```
其中核心的物件`struct file`，則是在`include/linux/fs.h`中，因為體積龐大，這裡就不列出了。值得注意的是，`fdget_pos`回傳的是一個實體物件，而不是指標！這的確是令人詫異的事情，究其原因，
```
 49 static inline struct fd __to_fd(unsigned long v)
 50 {
 51         return (struct fd){(struct file *)(v & ~3),v & 3};
 52 }
 53        
 54 static inline struct fd fdget(unsigned int fd)
 55 {      
 56         return __to_fd(__fdget(fd));
 57 }      
 58        
 59 static inline struct fd fdget_raw(unsigned int fd)
 60 {      
 61         return __to_fd(__fdget_raw(fd));
 62 }      
 63        
 64 static inline struct fd fdget_pos(int fd)
 65 {      
 66         return __to_fd(__fdget_pos(fd));
 67 }
```
64行的inline函數，最後透過`__to_fd`（也是inline）回傳了一個現作的`struct fd`物件，而且可以看到上面有一個`v & ~3`的技巧，這是因為`struct file`被宣告成align到4個byte，所以當66行的`__fdget_pos(fd)`給出一個指到某個`struct file`的結構體的`unsigned long`之後，就透過這個技巧取得真正的指標。至於`flags`，也就是後面`v & 3`的部份，目前筆者還沒有找到確切的用法在何處，暫且作為一個持續探索的動力放置。

第二部份即是寫入資料到取得的檔案了。判斷完`f`具有合法的`file`結構體之後，必須取得當前的檔案位置`pos`，否則會寫到錯誤的地方去。接下來就進入虛擬檔案層的寫入函數`vfs_write`之中，
```
 544 ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
 545 {       
 546         ssize_t ret;
 547         
 548         if (!(file->f_mode & FMODE_WRITE))
 549                 return -EBADF;
 550         if (!(file->f_mode & FMODE_CAN_WRITE))
 551                 return -EINVAL;
 552         if (unlikely(!access_ok(VERIFY_READ, buf, count)))
 553                 return -EFAULT;
 554         
 555         ret = rw_verify_area(WRITE, file, pos, count);
 556         if (!ret) {
 557                 if (count > MAX_RW_COUNT)
 558                         count =  MAX_RW_COUNT;
 559                 file_start_write(file); 
 560                 ret = __vfs_write(file, buf, count, pos);
 561                 if (ret > 0) {
 562                         fsnotify_modify(file);
 563                         add_wchar(current, ret);
 564                 }
 565                 inc_syscw(current);
 566                 file_end_write(file);
 567         }
 568         
 569         return ret;
 570 }       
```
簡略地說，547~555行都在進行這個寫入是否合法的判斷，通過之後才能進入556~567行的`if`區塊中。其中，由`file_start_write`和`file_end_write`成對包起來的是`__vfs_write`，
```
 506 ssize_t __vfs_write(struct file *file, const char __user *p, size_t count,
 507                     loff_t *pos)
 508 {      
 509         if (file->f_op->write)
 510                 return file->f_op->write(file, p, count, pos);
 511         else if (file->f_op->write_iter)
 512                 return new_sync_write(file, p, count, pos);
 513         else
 514                 return -EINVAL;
 515 }
```
`__vfs_write`秀了一把物件導向功夫，將核心程式執行的流程交棒給這個寫入的對象所定義的`write`當中。這個流程會如何繼續執行？無論進入哪一個判斷，最終都必須仰賴`file`內定義的檔案操作方法而決定呼叫的下一步。理論上來推測，如果這是某個USB硬體，則可能對應到該硬體的驅動程式中；若是Hello World，則會到tty終端機的`write`方法。

這不是靜態追蹤所能夠使用的情境，因為是到了執行期才能夠判斷一個寫入的系統呼叫該對應到哪些檔案相關的操作。所以，就以這個例子引入動態追蹤核心程式碼的方法吧！

---
### 動態追蹤

由於是第一次使用動態追蹤工具，也就是qemu+gdb的組合拳技巧，筆者建議各位參考[這個](http://files.meetup.com/1590495/debugging-with-qemu.pdf)，若是如筆者一樣採用更方便的libvirt管理，則參考這組[設定](http://wiki.libvirt.org/page/QEMUSwitchToLibvirt#-S)。

首先，必須備妥有`DEBUG_INFO`的核心，然後採用上面的設定讓qemu跑起來。然後gdb的部份，我們如此下：
```
(gdb) target remote :1234
Remote debugging using :1234
native_safe_halt () at ./arch/x86/include/asm/irqflags.h:50
50	}
(gdb) break sys_write if fd == 1 && count == 13
Breakpoint 1 at 0xffffffff8122d260: file fs/read_write.c, line 599.
(gdb) 
```

這個1和13究竟有何魔術呢？考慮下面這個初學者程式碼：
```
     1	#include<stdio.h> 
     2	int main(){
     3		printf("Hello World!\n");
     4		return 0;
     5	}
```
將之編譯完之後使用strace的結果，會在後面得到：
```
[root@archvm ~]# strace ./a.out > /dev/null
execve("./a.out", ["./a.out"], [/* 17 vars */]) = 0
...
write(1, "Hello World!\n", 13)          = 13
...
```
的結果，所以這裡只是運用這個知識，用這個來當作有條件的中斷。事實上，筆者一開始嘗試無條件中斷，則在開機的過程中會需要非常多次的重啟（continue）debug動作，因為write這個呼叫實在是太常用了，這也是理所當然的事情。另外相當有趣的是，如果不這麼作的話，在一個ssh階段輸入指令的過程將會觀察到sshd背景服務的write到某個製造封包的buffer，以及每一次的鍵盤事件。

條件設置好之後，就可以開機進入。過程中可能會遇到其他也符合條件的中斷，這似乎是非固定行為，因為筆者有時候開機不會遇到。使用SSH連線進入虛擬主機，然後執行該程式，則gdb會跳出：
```
(gdb) cont
Continuing.
[Switching to Thread 6]

Thread 6 hit Breakpoint 1, SyS_write (fd=1, buf=33411088, count=13) at fs/read_write.c:599
599	SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
(gdb) cont
Continuing.
```
的訊息。如果想要檢驗一下buf內的內容，可以使用印出的功能，
```
(gdb) x/s buf
0x1fdd010:	"Hello World!\n"
(gdb) 
```
顯然這就是我們造成的write沒錯了。（話說筆者現在才想到應該要弄個Hello鐵人！之類的訊息，不過反正意思都一樣，就這樣吧...）

那麼就可以動態地來觀察靜態追蹤時有點難處理的那種物件導向式的call法了。這裡可以先在`vfs_write`的地方設中斷點，然後如法炮製進入`__vfs_write`：
```
(gdb) b vfs_write if buf==33411088
Breakpoint 2 at 0xffffffff8122bd80: file fs/read_write.c, line 545.
(gdb) cont
Continuing.

Thread 4 hit Breakpoint 2, vfs_write (file=0xffff88003b868100, buf=0x1fdd010 "Hello World!\n", count=13, pos=0xffff88003abebf18)
    at fs/read_write.c:545
545	{
(gdb) b __vfs_write if buf==33411088
Breakpoint 3 at 0xffffffff8122b030: file fs/read_write.c, line 508.
```

進入`__vfs_write`之後，我們可以看到透過`f_op`包裝的部份，但是這次我們可以step進去了。（筆者過程中有些操作不當使得gdb掛掉，所以這裡的變數實際位置和上面不太一樣）
```
(gdb) step
tty_write (file=0xffff88003d01b300, buf=0x2201010 "Hello World!\n", count=13, ppos=0xffff88003c05ff18) at drivers/tty/tty_io.c:1238
(gdb) 
```
我們終於看到這裡的結果是，`file`所定義的檔案操作方法裡面的`write`指向到了`tty_write`，這也是終端機作為一個檔案的寫入方法。不知道開發過核心模組的讀者們是否也和筆者一樣，曾經有過`write()`的使用者空間呼叫和`file_operations`的`write`方法傳入參數不同的疑惑？從vfs_write的呼叫開始其實就已經默默地將參數轉化成為核心空間處理的形式了。

---
### 結論

本文瀏覽了`write`系統呼叫的主要功能，也就是使用者空間有感的那一部份，並且稍微觸及到核心空間特有的面向。然而，本文仍然跳過了許多部份，比方像是`__fdget_pos`函數如何將整數對應到一個`file`結構和它的flag；或是成對的`file_start_write`和`file_end_write`分別對inode做了哪些判斷；`fdput_pos`如何與kernel thread安排工作扯上關係；或是`tty_write`實際上做了哪些終端機的操作。

筆者會盡量補完上述的未竟之業。接下來預期的寫作流程，是從C初學者的心路歷程繼續下去，write體驗過之後是read，然後是open和close。有了這最基本的4項操作之後，再來集結成一個整體視角，或許更能呈現本文在探索這個單一系統呼叫時比較不方便觸及的面向。

感謝各位邦友，我們明天再會。
