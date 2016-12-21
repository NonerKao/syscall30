### 前情提要

前兩日一口氣度過了兩個很精實的系統呼叫。雖然筆者沒有能夠看懂並解釋所有的細節，但這為將來的學習無疑鋪設了入門磚。無論如何，接下來幾日，要來介紹的是跟記憶體相關的幾個呼叫。今日的主角則是`brk`。

---
### 介紹

單看`brk`三個字也是讓人霧裡看花，一樣先看看手冊：
```

NAME
       brk, sbrk - change data segment size

SYNOPSIS
       #include <unistd.h>

       int brk(void *addr);

       void *sbrk(intptr_t increment);
...
```
後續的敘述是這麼說的：`brk`系統呼叫會改變**program break**的值，而這個值是程序的資料區段（data segment）的結束位置所在。`brk`做的事情，也就是挪移這個位置到`addr`的地方去。

這功能可以做什麼呢？其實這就是支撐起`malloc()`的台柱。在`brk(2)`手冊的`NOTE`一節也特別警告使用者，`malloc`如此方便好用，就不要花時間用這個底層的系統呼叫了。雖說如此，但我們意在探究其中奧妙，所以當然還是要不聽話一下了。

雖說如此，但是突然間要筆者生出一個程序的program break所在，還真不知從何找起。其實這段期間的trace，筆者雖然程式碼無法解釋到最精隨的深處，卻也了悟了一個真正的道理，在此分享與各位讀者。那就是：**認真看手冊，一定有收穫！**

在同一個頁面當中還有介紹另外一個呼叫`sbrk`，用法是傳入一個記憶體增加量，如果這個要求合理的話，那麼系統就會配置那個大小的記憶體，然後回傳**舊的program break**值，來作為新要求的記憶體區段的起始位址。我們先寫個簡單的程式印證一下：
```
  1 #include<stdio.h>
  2 #include<stdlib.h>
  3 #include<unistd.h>
  4          
  5 int main(){
  6         char *ptr1 = sbrk(0);
  7         char *ptr2;
  8         sleep(1);
  9          
 10         brk(ptr1 + 512);
 11         sleep(1);
 12          
 13         ptr2 = (char*)malloc(512);
 14         sleep(1);
 15          
 16         free(ptr2);                                                                                                                       
 17         sleep(1);
 18         //free(ptr1);
 19         pause();
 20 }        
```
簡單來說就是第6行先取得program break所在之處，然後第8行配置一塊512位元組的空間（成功的話會回傳0，這裡偷懶一下），13行用`malloc`配置一樣大小的空間，16行把`ptr2`給`free`掉，18行有一個`free(ptr1)`未遂，是因為這個呼叫下去會出現core dump。最後令這個程式使用`pause`暫停執行。

用`strace`監控試跑一次，會得到類似這樣的結果，
```
[noner@heros 21-brk]$ strace ./a.out 
execve("./a.out", ["./a.out"], [/* 41 vars */]) = 0
...
brk(NULL)                               = 0x1f7a000
nanosleep({1, 0}, 0x7ffd9a47f220)       = 0
brk(0x1f7a200)                          = 0x1f7a200
nanosleep({1, 0}, 0x7ffd9a47f220)       = 0
brk(0x1f9b200)                          = 0x1f9b200
brk(0x1f9c000)                          = 0x1f9c000
nanosleep({1, 0}, 0x7ffd9a47f220)       = 0
brk(0x1f9b000)                          = 0x1f9b000
nanosleep({1, 0}, 0x7ffd9a47f220)       = 0
pause(
```
值得強調的是，**`brk`的wrapper API和核心的API不一樣**，核心的API會回傳當前的program break，這顯示在上述結果中。最一開始的`sbrk(0)`被轉化成是一個`brk(NULL)`。`malloc`應該做了很多事情，所以我們可以看到在它的呼叫期間引用了兩次`brk`，一次應該是配置用（而且那時它認為的program break從`0x1fb000`開始），第二次則是為之後的`malloc/free`作準備。從這個運作機制來猜想，這些配置或釋放的動作都是以`1 page = 4096 bytes = 0x1000 bytes`為單位。所以`free`被呼叫的時候，最後一個`brk`直接把program break設到一個整整小一個page的地方去。

趁著暫停偷偷觀察一下這個程序在proc檔案系統底下的記憶體資訊，從`/proc/<pid>/maps`可以看到：
```
[noner@heros ~]$ cat /proc/4432/maps 
00400000-00401000 r-xp 00000000 08:02 48104607                           /home/noner/FOSS/git/syscall30/21-brk/a.out
00600000-00601000 r--p 00000000 08:02 48104607                           /home/noner/FOSS/git/syscall30/21-brk/a.out
00601000-00602000 rw-p 00001000 08:02 48104607                           /home/noner/FOSS/git/syscall30/21-brk/a.out
01f7a000-01f9b000 rw-p 00000000 00:00 0                                  [heap]
...
```

---
### 正常的使用情境
稍微修改這個程式看看如果都使用正常的`malloc`會怎麼樣：
```
  1 #include<stdio.h>
  2 #include<stdlib.h>
  3 #include<unistd.h>
  4          
  5 int main(){
  6         char *ptr1 = sbrk(0);
  7         char *ptr2;
  8         sleep(1);
  9          
 10         ptr1 = (char*)malloc(512);
 11         sleep(1);
 12          
 13         ptr2 = (char*)malloc(512);
 14         sleep(1);
 15          
 16         free(ptr1);
 17         sleep(1);
 18          
 19         free(ptr2);
 20         sleep(1);
 21          
 22         ptr1[100] = ptr1[200] = 'a';
 23         ptr2[100] = ptr2[200] = 'b';                                                                                                      
 24 }        
```
這絕對是很奇怪的程式範例，但是正好可以驗證筆者的環境下跑出來的結果，請看`strace`：
```
...
brk(NULL)                               = 0x1e94000
nanosleep({1, 0}, 0x7fff8cc44d70)       = 0
brk(0x1eb5000)                          = 0x1eb5000
nanosleep({1, 0}, 0x7fff8cc44d70)       = 0
nanosleep({1, 0}, 0x7fff8cc44d70)       = 0
nanosleep({1, 0}, 0x7fff8cc44d70)       = 0
nanosleep({1, 0}, 0x7fff8cc44d70)       = 0
exit_group(0)                           = ?
+++ exited with 0 +++
[noner@heros 21-brk]$ 
```
也就是說`ptr1`配置了記憶體之後，因為這裡配置的是整個頁面，所以`malloc`內部做了一些機制，使得這個程式接下來完全無視後續的一個`malloc`和兩個`free`呼叫，22及23行的**看似**違法存取，實際上也安然無事的結束了。若是調整兩個指標需要的記憶體量，也有可能得到不一樣的結果。

> 這些`malloc`的反應當然是很令人好奇的行為，但是再trace下去的話就要把整個`glibc`翻出來看，目前只能暫時打住以維持專注於系統呼叫的任務。

---
### 追蹤

這個系統呼叫在`mm/mmap.c`之中：

> 事實上，若是使用`grep`工具尋找`SYSCALL_DEFINE1(brk,`則會發現有兩個定義，另一個在`mm/nommu.c`之中。顯然系統中是否有硬體的記憶體管理單元將會影響核心如何運行。

```
 174 SYSCALL_DEFINE1(brk, unsigned long, brk)
 175 {       
 176         unsigned long retval;
 177         unsigned long newbrk, oldbrk;
 178         struct mm_struct *mm = current->mm;
 179         unsigned long min_brk;
 180         bool populate;
 181         
 182         if (down_write_killable(&mm->mmap_sem))
 183                 return -EINTR;
...
 196         min_brk = mm->start_brk;
 197
 198         if (brk < min_brk)
 199                 goto out;
```
一開始的部份的判斷是呼應手冊上所謂**合理的範圍**的檢查的一部分。中間使用`rlimit`機制檢查是否可能導致超過這個程序的限制的配置（程式碼略過），然後是：
```
 211         newbrk = PAGE_ALIGN(brk);
 212         oldbrk = PAGE_ALIGN(mm->brk);
 213         if (oldbrk == newbrk)
 214                 goto set_brk;
 215         
 216         /* Always allow shrinking brk. */
 217         if (brk <= mm->brk) {
 218                 if (!do_munmap(mm, newbrk, oldbrk-newbrk))
 219                         goto set_brk;
 220                 goto out;
 221         }
```
`PAGE_ALIGN`是個巨集，讓傳入的位址針對`PAGE_SIZE`對齊。`mm`是一個來自當前程序的成員結構，代表的意義是記憶體的使用與管理，由這裡提取的`brk`成員即是原本的program break。這個系統呼叫函式最後的`goto`旗標只有兩個，剛好在這個片段全部都可以看到；其中`set_brk`是**有可能會設定新的brk**的出口，`out`則是**什麼也不做**的出口。213行表示這一次的呼叫的差距在一個page之內，直接前往設定的部份；217行如果`brk`小於或等於原本的brk，呼叫`do_munmap`函數。我們之後會在介紹`munmap`的部份重新遇到這個函數，這裡就先行跳過。

```
 223         /* Check against existing mmap mappings. */
 224         if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
 225                 goto out;
 226                  
 227         /* Ok, looks good - let it rip. */
 228         if (do_brk(oldbrk, newbrk-oldbrk) < 0)
 229                 goto out;
...
```
`find_vma_intersection`在`include/linux/mm.h`之中，內容是：
```
2151 /* Look up the first VMA which intersects the interval start_addr..end_addr-1,
2152    NULL if none.  Assume start_addr < end_addr. */
2153 static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)    
2154 {
2155         struct vm_area_struct * vma = find_vma(mm,start_addr);
2156  
2157         if (vma && end_addr <= vma->vm_start)
2158                 vma = NULL;
2159         return vma;
2160 }
```
註解的意思是，要在給定的起始值（傳入的oldbrk）和結束值（傳入的newbrk+1頁）之間找找看有沒有既有的頁面的意思，有就回傳，這會導致`brk`呼叫的224行的判斷失敗，前往`out`出口。

226行執行`do_brk`，這是一個蠻長的函數，註解稱之為**比較簡單一點的`do_mmap`**，如果會回傳小於零的值的話，大部分都是`-ENOMEM`比較多，這種情況當然就應該要從`out`離開。如果回傳0，也就是成功配置到記憶體並且映射到該虛擬記憶體位址的話，那就會接著執行接下來的`set_brk`出口了。

---
### 結論

記憶體管理的第一個系統呼叫選擇了`brk`，是因為C語言初學者至少在學習一陣子之後就會開始使用動態配置記憶體的功能。但是顯然今天這樣看下來，有許多尚未明瞭的部份，比方說核心深層用來管理記憶體的結構是什麼？程序內的成員`struct mm_struct`是什麼？與虛擬記憶體映射相關的結構`struct vm_area_struct`又是什麼？它們如何被使用？這些問題筆者都會在後續的`mmap`及`munmap`中繼續探索。感謝各位讀者的閱讀，我們明日再會！
