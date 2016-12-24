### 前情提要

昨天介紹了`mmap`的內容，本日則是要看看它的反操作：`munmap`。

---
### 介紹

```
NAME
       mmap, munmap - map or unmap files or devices into mem‐
       ory

SYNOPSIS
       #include <sys/mman.h>

       void *mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset);
       int munmap(void *addr, size_t length);
```
重複看一次手冊。有別於`mmap`可能需要指定諸般設定，取消映射的動作的`munmap`的就只需要指定被映射的位址與長度。老樣子，成功的話會回傳0，失敗的話則會回傳-1外加`errno`的設置（這是指wrapper的狀況，實際上Linux的系統呼叫應該會回傳錯誤代碼交給C函式庫處理）。

---
### 追蹤

```
2528 SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
2529 {
2530         int ret;
2531         struct mm_struct *mm = current->mm;
2532 
2533         profile_munmap(addr);
2534         if (down_write_killable(&mm->mmap_sem))
2535                 return -EINTR;
2536         ret = do_munmap(mm, addr, len);
2537         up_write(&mm->mmap_sem);
2538         return ret;
2539 }
```
2533的`profile_munmap`函式做的事情是在`kernel/notifier.c`之中註冊一些類似callback的函數，使得核心能夠紀錄`munmap`在這裡試圖要取消`addr`位址的映射；具體的函式名稱叫做`blocking_notifier_call_chain`，之前其實也多次出現過，但至今筆者都跳過了。無論如何，這個呼叫的核心內容是2536行的`do_munmap`：
```
2429 int do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
2430 {
2431         unsigned long end;
2432         struct vm_area_struct *vma, *prev, *last;
2433 
2434         if ((offset_in_page(start)) || start > TASK_SIZE || len > TASK_SI     ZE-start)
2435                 return -EINVAL;
2436 
2437         len = PAGE_ALIGN(len);
2438         if (len == 0)
2439                 return -EINVAL;
2440 
2441         /* Find the first overlapping VMA */
2442         vma = find_vma(mm, start);
2443         if (!vma)
2444                 return 0;
...
```
使用`find_vma`（在`rbk`一文中有看過，內容大多是紅黑數的操作）取得包含傳入位址的第一個虛擬記憶體區域。

> 這裡筆者覺得`vma`找不到的情況回傳0的成功條件很奇怪。因為在`find_vma`函數中，第一次會呼叫`vmacache_find`函式，這會設法從cache中撈出登錄的虛擬記憶體位址資訊。如果沒有的話，也會透過當前程序所屬的紅黑樹節點查找，以迴圈的方式找出對應的節點，離開迴圈之後，如果有找到的話，使用`vmacache_update`更新並回傳，可是如果沒找到應該會直接回傳`NULL`......在回到上面的2444行，難道沒有找到的情況視為成功嗎？

```
2445         prev = vma->vm_prev;
2446         /* we have  start < vma->vm_end  */
2447 
2448         /* if it doesn't overlap, we have nothing.. */
2449         end = start + len;
2450         if (vma->vm_start >= end)
2451                 return 0;
```
筆者至今仍不明白為什麼將`vma`指向前一個頁面就能夠保證`start < vma->vm_end`。然後接下來是沒有重疊的部份的話就不做事回傳0。`vma->vm_start`應該是這個`vma`代表的虛擬記憶體區域的起始位址，這要是比預期的範圍的結束`end`還大，就表示完全沒有重複的部份了。
```
2460         if (start > vma->vm_start) {
2461                 int error;
2462 
2463                 /*
2464                  * Make sure that map_count on return from munmap() will
2465                  * not exceed its limit; but let map_count go just above
2466                  * its limit temporarily, to help free resources as expec     ted.
2467                  */
2468                 if (end < vma->vm_end && mm->map_count >= sysctl_max_map_     count)
2469                         return -ENOMEM;
2470 
2471                 error = __split_vma(mm, vma, start, 0);
2472                 if (error)
2473                         return error;
2474                 prev = vma;
2475         }
```
2460行判斷的是指定的區域的起點至少在這個VMA的內部。2468行則代表這個VMA內部有一小區要被取消映射，這會造成VMA的分割。
```
2477         /* Does it split the last one? */
2478         last = find_vma(mm, end);
2479         if (last && end > last->vm_start) {
2480                 int error = __split_vma(mm, last, end, 1);
2481                 if (error)
2482                         return error;
2483         }
```
對於指定區域的終點也採取類似的作法，這裡的判斷是看終點是否位在最後一個頁面的管轄範圍。值得一提的是，`__split_vma`的最後一個參數的命名是`new_below`，也就是是否會有新的最後一頁產生。
```
2484         vma = prev ? prev->vm_next : mm->mmap;
2485 
2486         /*
2487          * unlock any mlock()ed ranges before detaching vmas
2488          */
2489         if (mm->locked_vm) {
2490                 struct vm_area_struct *tmp = vma;
2491                 while (tmp && tmp->vm_start < end) {
2492                         if (tmp->vm_flags & VM_LOCKED) {
2493                                 mm->locked_vm -= vma_pages(tmp);
2494                                 munlock_vma_pages_all(tmp);
2495                         }
2496                         tmp = tmp->vm_next;
2497                 }
2498         }
```
2484的判斷和指定筆者目前也是一頭霧水...只能以後再探討了...2489~2498的部份就是一個迴圈解鎖指定的區域。
```
2500         /*
2501          * Remove the vma's, and unmap the actual pages
2502          */
2503         detach_vmas_to_be_unmapped(mm, vma, prev, end);
2504         unmap_region(mm, vma, prev, start, end);
2505 
2506         arch_unmap(mm, vma, start, end);
2507 
2508         /* Fix up all other VM information */
2509         remove_vma_list(mm, vma);
2510 
2511         return 0;
2512 }
```
2503行除了修改`mm`結構內的一些成員的狀態，還把有取消映射的頁面從快取中除掉。2504則處理一些與page table有關的東西。2506行的架構相依處理在x86不太可能發生（也就是通常並不啟用記憶體保護延伸的MPX功能）。最後`remove_vma_list`釋放所有未配置的記憶體部份。

---
### 結論

結果對於`vma`的操作還是不很熟悉，無法解讀一部分為什麼要指來指去的指令。但是對於記憶體配置、映射、權限管理的核心處理大致上有了些了解。接下來就要持續精進了。

最後一週的系統呼叫，筆者打算先安排一個跨行程通訊常用的`pipe`，幾乎所有人在操作shell環境時都有用過；接下來是連續四日的網路相關呼叫；最後一個則當作一個祕密留待最後揭曉。無論如何，我們明天再見！
