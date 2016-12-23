### 前情提要

程序隨時都要使用到虛擬記憶體，從配置到使用的過程，我們都需要許多的機制。其中我們已經介紹了動態配置記憶體的核心功能`brk`，以及設置存取權限給虛擬記憶體區段的`mprotect`。本日的主角則是`mmap`這個系統呼叫。

---
### 介紹

在看手冊的簡介之前，各位讀者如果不常接觸這個呼叫的話，應該會覺得`mmap`就是一個低階的記憶體相關指令，雖然聽說過但也沒有使用過幾次。事實上，`mmap`幾乎可以說是每一次執行程序的時候都會用到的系統呼叫，請看隨意一個程式的`strace`：
```
[demo@linux ~]$ strace /bin/ls 2>&1 | grep mmap
mmap(NULL, 167449, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7f26aabbe000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f26aabbc000
mmap(NULL, 2112504, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7f26aa7c1000
mmap(0x7f26aa9c4000, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x3000) = 0x7f26aa9c4000
mmap(NULL, 3791152, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7f26aa423000
mmap(0x7f26aa7b7000, 24576, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x194000) = 0x7f26aa7b7000
mmap(0x7f26aa7bd000, 14640, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) = 0x7f26aa7bd000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f26aabba000
mmap(NULL, 4223360, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7f26aa01b000
```

各位讀者也可以執行之前的`write`一文中的範例程式，就會發現就算是只有一個`printf`的程式，使用`strace`觀察仍然會發現許多`mmap`的部份。其中大部分是來自dynamic loader將函式庫對應到記憶體的內容。

參考手冊是這樣：
```
NAME
       mmap, munmap - map or unmap files or devices into memory

SYNOPSIS
       #include <sys/mman.h>

       void *mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset);
       int munmap(void *addr, size_t length);
```
也就是將檔案或是裝置的部份**映射**到記憶體上面。最主要可以將這種映射分為兩種，一個是將**檔案內容映射到程序所見的記憶體**，另一個則是**匿名映射**，更類似一種配置。傳入參數的部份，先介紹第五個的**fd**，因為這個檔案描述子的有效與否就對應到了前述的基本差異，我們也可以從上面節錄的片段看到有大約一半的`fd`被傳入為`-1`，並且可以看到他們都與`flags`參數的`MAP_ANONYMOUS`對應。第三個的`prot`參數基本上如昨日介紹的一樣。`addr`和`length`就是**預期配置的記憶體位置**和**該區域記憶體的長度**。`offset`則是用在檔案的映射的情況下，必須指定檔案的offset。回傳值則是最後真正給定的配置到的位置，如果失敗了則會回傳一個可辨識的失敗值。`flags`參數的意義比較複雜，是**當前程序對這個映射的區段做出更改的時候，該如何被其他也使用這個記憶體區段的程序知道**，後面會有一個有趣的小例子。

---
### 追蹤

根據手冊的`NOTE`一節，C函式庫的wrapper與系統呼叫的介面有一點點不同在於`offset`一個參數的一些調整，筆者推測多半也是對齊之類的問題，這裡就姑且忽略。一開始費了些手腳，才發現或許是因為這有點相依於處理器的因素，`mmap`在`arch/x86/kernel/sys_x86_64.c`之中，
```
 86 SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
 87                 unsigned long, prot, unsigned long, flags,
 88                 unsigned long, fd, unsigned long, off)
 89 {                
 90         long error;
 91         error = -EINVAL;
 92         if (off & ~PAGE_MASK)
 93                 goto out;
 94                  
 95         error = sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);                        
 96 out:             
 97         return error;
 98 }                
```
這裡先將offset稍微修改了一下，目的是想要讓這個呼叫能夠支援更大的檔案吧，因為反正都鐵了心只支援`PAGE_SIZE`的整數倍數，那就不如全部都挪移掉。之後引用了`mm/mmap.c`裡面的`mmap_pgoff`：
```
1305 SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
1306                 unsigned long, prot, unsigned long, flags,
1307                 unsigned long, fd, unsigned long, pgoff)
1308 {               
1309         struct file *file = NULL;
1310         unsigned long retval;
1311                 
1312         if (!(flags & MAP_ANONYMOUS)) {
1313                 audit_mmap_fd(fd, flags);
1314                 file = fget(fd);
1315                 if (!file)
1316                         return -EBADF;
1317                 if (is_file_hugepages(file))
1318                         len = ALIGN(len, huge_page_size(hstate_file(file)));
1319                 retval = -EINVAL;
1320                 if (unlikely(flags & MAP_HUGETLB && !is_file_hugepages(file)))
1321                         goto out_fput;
1322         } else if (flags & MAP_HUGETLB) {
```
1312行的判斷是先確定這不是個匿名頁的映射請求，然後透過`fd`取得該檔案。有了這一步之後，透過`is_file_hugepages`函數判斷對應的檔案系統有沒有支援大型記憶體頁面，筆者的Arch預設是支援的；如果有這個設定，那麼`len`的相關對齊計算當然是不能少的。
```
1322         } else if (flags & MAP_HUGETLB) {
1323                 struct user_struct *user = NULL;
1324                 struct hstate *hs;
1325 
1326                 hs = hstate_sizelog((flags >> MAP_HUGE_SHIFT) & SHM_HUGE_MASK);
1327                 if (!hs)
1328                         return -EINVAL;
1329 
1330                 len = ALIGN(len, huge_page_size(hs));
1331                 /*
1332                  * VM_NORESERVE is used because the reservations will be
1333                  * taken when vm_ops->mmap() is called
1334                  * A dummy user value is used because we are not locking
1335                  * memory so no accounting is necessary
1336                  */
1337                 file = hugetlb_file_setup(HUGETLB_ANON_FILE, len,
1338                                 VM_NORESERVE,
1339                                 &user, HUGETLB_ANONHUGE_INODE,
1340                                 (flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
1341                 if (IS_ERR(file))
1342                         return PTR_ERR(file);
1343         }
```
如果是支援大型記憶體頁面的檔案系統會有這些處理。`user`預設是NULL，即將在`mm/hugetlbfs/inode.c`的`hugetlb_file_setup`裡面透過`current_user`的呼叫回傳出來。這裡有一些大型頁面配置需要的過程，但在筆者關心的基礎功能上較少出現，這裡就不深入了。
```
1345         flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
1346 
1347         retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);
1348 out_fput:
1349         if (file)
1350                 fput(file);
1351         return retval;
1352 }
```
離開判斷之後，首先是1345的取消旗標效果。這兩個flag都是參考手冊中指定忽略的部份，其中尤其有趣的是`MAP_DENYWRITE`。原本這個旗標的用意是讓其他共用這個區域的程序試圖寫入的時候會有一個錯誤狀態`ETXTBUSY`並且有訊號回傳，結果後來發現這會變成有心人對系統的Denial-of-service。

接著就是`vm_mmap_pgoff`的呼叫，與原本的差異可以說是把`fd`轉換成核心內部用的檔案結構。這個函數在`mm/util.c`之中：
```
290 unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
291         unsigned long len, unsigned long prot,
292         unsigned long flag, unsigned long pgoff)
293 {
294         unsigned long ret;
295         struct mm_struct *mm = current->mm;
296         unsigned long populate;
297  
298         ret = security_mmap_file(file, prot, flag);
299         if (!ret) {
300                 if (down_write_killable(&mm->mmap_sem))
301                         return -EINTR;
302                 ret = do_mmap_pgoff(file, addr, len, prot, flag, pgoff,
303                                     &populate);
304                 up_write(&mm->mmap_sem);
305                 if (populate)
306                         mm_populate(ret, populate);
307         }
308         return ret;
309 }
```
從大架構來看，我們再次看到程序內的記憶體改動會在`down_write*`以及`up_write`的旗標保護之內進行，裡面顯然是核心的`do_mmap_pgoff`。外面的`mm_populate`，根據[這個patch](https://lkml.org/lkml/2012/12/20/494)的描述，是因為有時候這個動作會要檢查disk或其他瑣碎事情，這樣的事情應該避免出現在critical section之內才對；所以就在`do_mmap_pgoff`裡面決定是不是有必要出來populate這塊區域。

之前提到要研究`struct mm_struct`，這個結構定義在`include/linux/mm_types.h`裡面，也是一個120行左右的大結構，裡面有像是`owner`這個指到`struct task_struct`的成員、`map_count`代表擁有的虛擬記憶體區段、與`do_mmap_pgoff`一樣行別的函數指標`get_unmmaped_area`、代表虛擬記憶體區段清單的型別為`struct vm_area_struct`的`mmap`成員等等。

`do_mmap_pgoff`在`include/linux/mm.h`又轉了一手：
```
2040 static inline unsigned long
2041 do_mmap_pgoff(struct file *file, unsigned long addr,
2042         unsigned long len, unsigned long prot, unsigned long flags,
2043         unsigned long pgoff, unsigned long *populate)
2044 {
2045         return do_mmap(file, addr, len, prot, flags, 0, pgoff, populate);
2046 }              
```
又回到`mm/mmap.c`裡面的、之前在`brk`一文時也曾經看到的`do_mmap`呼叫，因為有點長，就用描述性的介紹一下重要的三個部份。首先第一個關鍵的呼叫在`get_unmapped_area`之中，這裡會驗證`addr`開始的`len`位元組的記憶體區段在要映射的單位是否合法，對於映射的來源端會取得`file->f_op->get_unmmaped_area`函數指標（或匿名映射的情況，使用`shmem_get_unmapped_area`），而映射的目的地則取得`current->mm->get_unmmaped_area`函數指標執行。第二個部份則是分別針對有無開啟檔案以及分享或私有的性質做一些判斷處理，最重要的就是`file->f_op->mmap`是否存在的判斷。最後則是`mmap_region`這個同樣也非常長的函數，其內部開始針對虛擬記憶體區段層級做些實事，比方說實際配置的過程（透過`kmem_cache_zalloc`）、檢查不同的vma之間是否需要合併、最後透過`vma_set_page_prot`做權限的設定。

---
### 結論

今天大致看過了`mmap`的過程，這牽涉到一些架構相關的處理（是否有大頁面支援），然後經過了一大段架構無關的程式碼之後，會透過函數指標的方式再去存取檔案系統相關的方法。接下來預計的`munmap`就是這個階段的反面，我們明天再會！
