### 前情提要

第一次探索mm子目錄底下的檔案，`brk`系統呼叫。記憶體是運算架構裡至關重要的一環，對於作業系統掌管程序的記憶體配置來說，也有許多的機制在裡面運作。今天要帶給各位讀者的系統呼叫則是：`mprotect`。

---
### 介紹

```
NAME
       mprotect - set protection on a region of memory

SYNOPSIS
       #include <sys/mman.h>

       int mprotect(void *addr, size_t len, int prot);
```
這個呼叫會設定記憶體區段**從`addr`到`addr+len-1`**的保護機制為**`prot`**的定義內容。其中`prot`的值可以是：

* `PROT_NONE`：根本無法被存取
* `PROT_READ`：該區段可以被讀取
* `PROT_WRITE`：該區段可以被寫入
* `PROT_EXEC`：該區段可以被當作機器碼執行

手冊`mprotect(2)`中列出一個範例程式，該程式會先配置4頁的記憶體，然後修改後面兩頁的性質為`PROT_READ`；之後再跑一個迴圈不斷地在剛配置的記憶體中寫入。在第三頁開頭的時候即會出現`SIGSEGV`的錯誤。

> 預設配置所得的記憶體的性質是可讀可寫。

這個呼叫也可以用來玩一些有趣的事情，比方說自我修改（self-modifying），因為可以透過這個方法把原本預設是`R-X`權限的程式區段改成可寫入。

---
### 追蹤程式碼

在`mm/mprotect.c`裡面，`mprotect`是個剛好一百行的函式：
```
355 SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
356                 unsigned long, prot)
357 {
358         unsigned long nstart, end, tmp, reqprot;
359         struct vm_area_struct *vma, *prev;
360         int error = -EINVAL;
361         const int grows = prot & (PROT_GROWSDOWN|PROT_GROWSUP);
362         const bool rier = (current->personality & READ_IMPLIES_EXEC) &&
363                                 (prot & PROT_READ);
364 
365         prot &= ~(PROT_GROWSDOWN|PROT_GROWSUP);
366         if (grows == (PROT_GROWSDOWN|PROT_GROWSUP)) /* can't be both */
367                 return -EINVAL;
368 
369         if (start & ~PAGE_MASK)
370                 return -EINVAL;
371         if (!len)
372                 return 0;
373         len = PAGE_ALIGN(len);
374         end = start + len;
375         if (end <= start)
376                 return -ENOMEM;
377         if (!arch_validate_prot(prot))
378                 return -EINVAL;
379 
380         reqprot = prot;
```
到380行為止，做了許多關於傳入的保護旗標的判斷以及位址是否合理的判斷。

> 筆者發現`mprotect(2)`有[新版本](http://man7.org/linux/man-pages/man2/mprotect.2.html)，裡面有更詳細的描述。

```
382         if (down_write_killable(&current->mm->mmap_sem))
383                 return -EINTR;
384 
385         vma = find_vma(current->mm, start);
386         error = -ENOMEM;
387         if (!vma)
388                 goto out;
389         prev = vma->vm_prev;
```
和昨天一樣，對於一個程序做記憶體管理時，會使用`current->mm->mmap_sem`旗標並令它`down_write_*`。然後385行取得一個vma，但疑惑的是，為什麼389行會要直接拿前一個`vm_area_struct`呢？重點留意項目，希望日後可以找出答案。
```
390         if (unlikely(grows & PROT_GROWSDOWN)) {
391                 if (vma->vm_start >= end)
392                         goto out;
393                 start = vma->vm_start;
394                 error = -EINVAL;
395                 if (!(vma->vm_flags & VM_GROWSDOWN))
396                         goto out;
397         } else {
398                 if (vma->vm_start > start)
399                         goto out;
400                 if (unlikely(grows & PROT_GROWSUP)) {
401                         end = vma->vm_end;
402                         error = -EINVAL;
403                         if (!(vma->vm_flags & VM_GROWSUP))
404                                 goto out;
405                 }
406         }
407         if (start > vma->vm_start)
408                 prev = vma;
```
這個片段的重點是依照`PROT_GROWS*`兩個旗標來判斷記憶體位置是否合理，若是不理則都直接送out的部份，要嘛錯誤是剛才設的`-ENOMEM`，要不然就是`-EINVAL`。所以正常的使用情況，應該完全不會進到這些判斷之中。最後的兩行又把`prev`指回`vma`，完全不知道發生了什麼事...
```
410         for (nstart = start ; ; ) {
411                 unsigned long newflags;
412                 int pkey = arch_override_mprotect_pkey(vma, prot, -1);
413 
414                 /* 在這裡我們知道 vma->vm_start <= nstart < vma->vm_end. */
415 
416                 /* 應用程式是否預期 PROT_READ 性質同時也代表 PROT_EXEC */
417                 if (rier && (vma->vm_flags & VM_MAYEXEC))
418                         prot |= PROT_EXEC;
419 
420                 newflags = calc_vm_prot_bits(prot, pkey);
421                 newflags |= (vma->vm_flags & ~(VM_READ | VM_WRITE | VM_EXEC));
422 
423                 /* newflags >> 4 shift VM_MAY% in place of VM_% */
424                 if ((newflags & ~(newflags >> 4)) & (VM_READ | VM_WRITE | VM_EXEC)) {
425                         error = -EACCES;
426                         goto out;
427                 }
```
412行提到的`pkey`，是一整套Linux獨有的安全機制，在這系列中不會有後續的探討。有興趣的讀者請自行參考[線上文件](http://man7.org/linux/man-pages/man7/pkeys.7.html)。420~427行計算並檢查新的權限旗標。
```
...
432 
433                 tmp = vma->vm_end;
434                 if (tmp > end)
435                         tmp = end;
436                 error = mprotect_fixup(vma, &prev, nstart, tmp, newflags);
437                 if (error)
438                         goto out;
439                 nstart = tmp;
440 
441                 if (nstart < prev->vm_end)
442                         nstart = prev->vm_end;
443                 if (nstart >= end)
444                         goto out;
```
在`mprotect_fixup`之中，會先檢查`vma`所代表的這一塊虛擬記憶體區域的權限旗標是否與傳入的`newflags`相同，若是相同就不必繼續執行而回傳成功的0；反之，代表有些修改需做，比方說若原先該虛擬記憶體區域結構管理的區域較指定修改權限的區域大，則有必要透過`mm/mmap.c`裡的`split_vma`函數分割該VMA。所有確認機制都完成之後，就會透過`vma_set_page_prot`及後續函數修改該區塊的保護狀態。444行是離開迴圈的條件，代表現在已經沒有頁面的保護狀態需要被更動了。

---
### 結論

記憶體相關的系統呼叫真的有看越多越心虛的感覺，明日筆者預計在閱讀`mmap`的同時，理解一下相關的資料結構，也會想辦法尋求一些既有的技術文章輔助閱讀；有了`mmap`的知識之後，`munmap`應該就會簡單許多了。我們明日再會！
