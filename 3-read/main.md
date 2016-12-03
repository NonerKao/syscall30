### 前情提要

首日我們在`uname`的暖身中，大概看到核心原始碼的樣貌；昨日則是由`write`起始，進入一個新的大章節中，雖說讀、寫、開啟、關閉這些功能在字面上都是檔案在做的事情，但正因為Linux承襲了萬物皆檔案的哲學，所以這些最通用的檔案處理系統呼叫，實際上就是最全面且常用的系統呼叫。

昨日筆者呈現了靜態和動態追蹤分別可以作到的事情，今日則讓我們再多往前探索一步吧！

---
### 靜態追蹤

`read`這個呼叫的原型與`write`一模一樣，
```
NAME
       read - read from a file descriptor

SYNOPSIS
       #include <unistd.h>

       ssize_t read(int fd, void *buf, size_t count);
```

其實，以核心原始碼論，它就在`fs/read_write.c`檔案中，`write`之上的位置，
```
 584 SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
 585 {
 586         struct fd f = fdget_pos(fd);
 587         ssize_t ret = -EBADF;
 588 
 589         if (f.file) {
 590                 loff_t pos = file_pos_read(f.file);
 591                 ret = vfs_read(f.file, buf, count, &pos);
 592                 if (ret >= 0)
 593                         file_pos_write(f.file, pos);
 594                 fdput_pos(f);
 595         }
 596         return ret;
 597 }
```
這看起來和`write`九成像，其實就連`vfs_read`也是，
```
 460 ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
 461 {
 462         ssize_t ret;
 463 
 464         if (!(file->f_mode & FMODE_READ))
 465                 return -EBADF;
 466         if (!(file->f_mode & FMODE_CAN_READ))
 467                 return -EINVAL;
 468         if (unlikely(!access_ok(VERIFY_WRITE, buf, count)))
 469                 return -EFAULT;
 470 
 471         ret = rw_verify_area(READ, file, pos, count);
 472         if (!ret) {
 473                 if (count > MAX_RW_COUNT)
 474                         count =  MAX_RW_COUNT;
 475                 ret = __vfs_read(file, buf, count, pos);
 476                 if (ret > 0) {
 477                         fsnotify_access(file);
 478                         add_rchar(current, ret);
 479                 }
 480                 inc_syscr(current);
 481         }
 482 
 483         return ret;
 484 }
```
這結構是完全一樣的，同樣有檢查的部份、有對同時可讀寫的數量判斷的部份、有個前置雙底線的版本、有個成功了之後（`ret > 0`條件）要知會檔案系統（fsnotify系列）、行程管理中的數據統計（`add_xchar`系列）、同樣有程序的輸入輸出紀錄（IO accounting）用的inc_syscx系列。

說到相異之處的話則是，`read`沒有相應的`file_start_read`或`file_end_read`。

雙底線版本`__vfs_read`的樣貌也是相當眼熟的，
```
 448 ssize_t __vfs_read(struct file *file, char __user *buf, size_t count,
 449                    loff_t *pos)
 450 {
 451         if (file->f_op->read)
 452                 return file->f_op->read(file, buf, count, pos);
 453         else if (file->f_op->read_iter)
 454                 return new_sync_read(file, buf, count, pos);
 455         else
 456                 return -EINVAL;
 457 }
```
一樣有炫目的物件導向操作，也一樣提供了除了`read`之外的可能。若是這個`file`結構操作不支援`read`，則依這個邏輯來看會轉向參考`read_iter`是否存在。筆者目前並不清楚這兩者的差異，簡單檢索了一下發現，tty終端機沒有讀寫`*iter`操作的註冊，而一般檔案系統如XFS或EXT4都只有註冊讀寫`*iter`操作，有興趣的讀者請先自行往相關的部份探索，許多資料位於`fs`子目錄下。

儘管使用者一定是透過`write`相關的操作才能知道自己在系統上做了什麼事情（否則何來資訊的取得呢？），`read`在台面下則是默默的做了許多事情，比方說許多使用者空間的背景服務，可能時不時地讀取一些系統檔案，這其中也有可能會使用到`read`（`scanf`或`fscanf`）或是其他進階的讀取相關的系統呼叫。

因為實在是太像了，本文就更深入地看看`write`一文中沒有完成的部份吧！

#### 謎之一：為什麼`fdget_pos`的內部過程會長成那個樣子？

回顧一下讀寫相同的開始片段：
```
 ...
 602         struct fd f = fdget_pos(fd);
 603         ssize_t ret = -EBADF;
 604 
 605         if (f.file) {
 606                 loff_t pos = file_pos_read(f.file);
 ...
```
如果回傳的`f`內部沒有`file`成員，則會回傳預設的`-EBADF`，也就是不好的檔案描述子的意思。深入來看`fdget_pos`：
```
 49 static inline struct fd __to_fd(unsigned long v)
 50 {       
 51         return (struct fd){(struct file *)(v & ~3),v & 3};
 52 }
 ...
 64 static inline struct fd fdget_pos(int fd)
 65 {
 66         return __to_fd(__fdget_pos(fd));
 67 }        
```
也就是說，當`__fdget_pos`依照`fd`取得一個`unsigned long`之後，在`__to_fd`中，會將後面兩個bit化為`flag`成員、其餘的62個bit化為一個`struct file`的結構體的指標，然後打包成一個`struct fd`回傳。此處的關鍵是內部的`__fdget_pos`：
```
773 unsigned long __fdget_pos(unsigned int fd)
774 {
775         unsigned long v = __fdget(fd);
776         struct file *file = (struct file *)(v & ~3);
777 
778         if (file && (file->f_mode & FMODE_ATOMIC_POS)) {
779                 if (file_count(file) > 1) {
780                         v |= FDPUT_POS_UNLOCK;
781                         mutex_lock(&file->f_pos_lock);
782                 }
783         }
784         return v;
785 }
```
`__fdget`將會回傳一個能夠代表這個檔案的東西，留待後段再trace；這樣看來，這整段的重點即在判斷式。如果**檔案存在**並且**檔案的模式有FMODE_ATOMIC_POS這個特性**。前者只要學過C語言就能體會，後者是什麼意思？
`fmode_t f_mode`在`struct file`的定義中，`fmode_t`則在`include/linux/types.h`裡面定義成`unsigned __bitwise__`的型態。`FMODE_ATOMIC_POS`則是在`include/linux/fs.h`中的聚集，為**本檔案是否需要atomic存取**的意思。所以若進入了這個條件區塊，就會緊接著判斷這個檔案當前的被存取總數是否大於一個，如果否的話仍然沒有別的事要做，是的話則要給予回傳的`v`這個指標附帶flag，這裡的FDPUT_POS_UNLOCK則是要求之後的某個`fdput`系列函數必須負責將mutex鎖解開。

> 這裡筆者引用了系列文之外的知識，或許之後有機會正式驗證，但此處先做個備註。在核心當中，get/put這兩個成對出現的概念，通常會與**被存取的數量控制**有關，get代表讓該物件存取數增加，put則反之。

所以這個`v`，也就是一個指到這個檔案的指標究竟怎麼來的？依賴於更前面的呼叫`__fdget`（762行）：
```
745 static unsigned long __fget_light(unsigned int fd, fmode_t mask)
746 {
747         struct files_struct *files = current->files;
748         struct file *file;
749 
750         if (atomic_read(&files->count) == 1) {
751                 file = __fcheck_files(files, fd);
752                 if (!file || unlikely(file->f_mode & mask))
753                         return 0;
754                 return (unsigned long)file;
755         } else {
756                 file = __fget(fd, mask);
757                 if (!file)
758                         return 0; 
759                 return FDPUT_FPUT | (unsigned long)file;
760         }
761 }
762 unsigned long __fdget(unsigned int fd)
763 {
764         return __fget_light(fd, FMODE_PATH);
765 }
```
看來`__fdget`是補上`FMODE_PATH`的`__fget_light`。這個flag深入搜尋，定義在`FMODE_ATOMIC_POS`的上面，註解說**這個檔案在`open`的時候附帶了`O_PATH`參數，幾乎無法使用**，在核心源碼樹中找不到關於這個旗標的太多資訊，因此應該參考`open(2)`的手冊，此處就略過以求簡潔，簡單來說現階段只須知道**無法讀寫**，應該也就夠用了。另外，筆者明天就會緊接著介紹`open`系統呼叫，敬請期待。

於是我們應該參考`__fdget_light`的內部邏輯，才有辦法知道傳入這個flag的用意為何。首先，核心會從`current`，也就是當前執行的程序結構中取得`files`變數，代表當前程序開啟檔案的狀態，`count`就是檢驗現在開啟的檔案數量是否為一，如果是的話就可以從`__fcheck_files`取得`fd`代表的檔案，在**檔案不存在**或是**（不太可能）這個檔案符合傳入`mask`的情況下（目前看起來都是指那些開啟時使用了`O_PATH`的狀況）**回傳0，反之則回傳所獲得的檔案`file`；如果開啟的檔案數量不為一，則在`__fget`呼叫後，最後結果會要多設一個`FDPUT_PUT`的flag，這同樣是在知會之後的put動作需要調整存取數。

如果這裡進入了必須使用`__fget`呼叫的條件，則：
```
694 static struct file *__fget(unsigned int fd, fmode_t mask)
695 {
696         struct files_struct *files = current->files;
697         struct file *file;
698 
699         rcu_read_lock();
700 loop:
701         file = fcheck_files(files, fd);
702         if (file) {
703                 /* File object ref couldn't be taken.
704                  * dup2() atomicity guarantee is the reason
705                  * we loop to catch the new file (or NULL pointer)
706                  */
707                 if (file->f_mode & mask)
708                         file = NULL;
709                 else if (!get_file_rcu(file))
710                         goto loop;
711         }
712         rcu_read_unlock();
713 
714         return file;
715 }
```
這裡出現了核心空間很重要的一個關鍵字：`rcu`。筆者此時不認為這30天的旅程中會介紹這個重要的觀念，所以暫時不解釋這個同步機制，請參考外部資料如[這篇整理](http://rd-life.blogspot.tw/2009/05/rcu_26.html)。這裡會用到的`fcheck_files`會檢查有沒有一些上鎖的狀態，之後呼叫之前也出現過的`__fcheck_files`。
```
 80 static inline struct file *__fcheck_files(struct files_struct *files, unsigned int fd)
 81 {
 82         struct fdtable *fdt = rcu_dereference_raw(files->fdt);
 83 
 84         if (fd < fdt->max_fds)
 85                 return rcu_dereference_raw(fdt->fd[fd]);
 86         return NULL;
 87 }       
```
一連出現了兩個`rcu_dereference_raw`，語意上自然就是解開這個參照的存取，只不過需要rcu的保護機制。`files`屬於`struct files`型別，其中的`fdt`成員是`struct fdtable`型別，額外加上一個`__rcu`性質的宣告，因此不能單純的存取。`fdt`存取`fd`時（這個型別是`struct file **`，存有檔案指標的陣列），也是類似的狀況。程式碼字面上的意義容易理解，就是取得了這個程序的檔案table之後，檢查`fd`是否小於這個檔案table允許的最大值，然後取得索引位於`fd`的檔案指標。

至此，`read`與`write`開頭相似的部份的追尋，終於結束。

#### 謎之二：與終端機相關的部份究竟做了什麼？

在drivers/tty/tty_io.c之中，我們可以找到`__vfs_read`之後的去向：
```
1060 static ssize_t tty_read(struct file *file, char __user *buf, size_t count,
1061                         loff_t *ppos)
1062 {
1063         int i;
1064         struct inode *inode = file_inode(file);
1065         struct tty_struct *tty = file_tty(file);
1066         struct tty_ldisc *ld;
1067 
1068         if (tty_paranoia_check(tty, inode, "tty_read"))
1069                 return -EIO;
1070         if (!tty || tty_io_error(tty))
1071                 return -EIO;
1072 
1073         /* We want to wait for the line discipline to sort out in this
1074            situation */
1075         ld = tty_ldisc_ref_wait(tty);
1076         if (!ld)
1077                 return hung_up_tty_read(file, buf, count, ppos);
1078         if (ld->ops->read)
1079                 i = ld->ops->read(tty, file, buf, count);
1080         else
1081                 i = -EIO;
1082         tty_ldisc_deref(ld);
1083 
1084         if (i > 0)
1085                 tty_update_time(&inode->i_atime);
1086 
1087         return i;
1088 }
```

核心的部份在於1073行之後提到的[line discipline](https://en.wikipedia.org/wiki/Line_discipline)，是終端機子系統裡面的一個抽象層，介在character device與真正的硬體驅動程式之間。若是由`tty_ldisc_ref_wait`回傳的`ld`沒有意義，則`hung_up_tty_read`會直接回傳0，也就是沒有任何東西真正被讀取的意思。從第二個判斷區塊可以知道，這個`ld`必須要定義好它的操作方法，而再由它的read執行真正的動作。

這會導向`drivers/tty/n_tty.c`（這個檔案本身的開場白闡明了這是一個很難讀的檔案，毛很多）裡面的`n_tty_read`函數。這個函數就已經太長太難讀，也許之後再行補完。

#### 還有些我感興趣的謎被你跳過了...

當然！筆者並不是身為核心駭客而來發這系列，而是想要透過這個挑戰的機會多認識一點Linux核心。有不夠詳盡之處，歡迎留言討論！事實上，後續篇章中也可能會補完一些筆者自己比較心虛的部份。

---
### 結論

本文回頭看了與提取檔案結構相關的部份，也介紹了`printf`與`scanf`這類標準函式庫呼叫最後對應到的基礎介面`write`/`read`在終端機部份的機制。接下來就要準備迎來剩下的兩組檔案介面`open`與`close`。感謝各位讀者，我們明天再會。
