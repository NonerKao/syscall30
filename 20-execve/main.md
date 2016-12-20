### 前情提要

與訊號處理有關的系統呼叫告一段落，新篇章也尚未開始的本日，介紹一個很重要卻難以歸類的系統呼叫：`execve`。

---
### 介紹

```
NAME
       execve - execute program

SYNOPSIS
       #include <unistd.h>

       int execve(const char *filename, char *const argv[],
                  char *const envp[]);

...
```
我們已經看過程序管理用的系統呼叫`fork`與`clone`，所以我們知道什麼樣的核心支援能夠使得電腦運行時有許多程序可以執行，但是我們仍然不清楚這些程序為何能夠執行不同的程式，其中的關鍵就在這個`execve`。常用的用法來自shell這樣的系統程式，命令列自己是一個主程序，當它接收到使用者輸入的執行命令時，先是創建一個子程序，然後該子程序運用這個系統呼叫分別指定三項參數：

* `filename`：字串，代表該可執行檔案的名稱
* `argv`：字串陣列，代表該程式接收到的命令列參數
* `envp`：字串陣列，代表該程式執行時的環境變數

其中，C語言撰寫的程式可以透過`int main(int argc, char *argv[])`標頭與`char *getenv(char*)`工具函式輕易地取得這三者的內容。這三個性質即是核心為了這個程式而必須準備的資訊。

> 冷門知識，其實用C寫程式的時候也可以使用`int main(int argc, char *argv[], char *envp[])`當作標頭喔！

這個系統呼叫在C library的層級有許多不同的wrapper，提供不同的方便性。有一個就算是普通的使用者程式可能也常常用到的`system()`函數，這本身就會引用一個`fork`和一個`execve`系統呼叫。

---
### 追蹤`execve`

有趣的是，這個系統呼叫不是在`kernel/`之下，而是在`fs/exec.c`：
```
1777 int do_execve(struct filename *filename,
1778         const char __user *const __user *__argv,
1779         const char __user *const __user *__envp)
1780 {
1781         struct user_arg_ptr argv = { .ptr.native = __argv };
1782         struct user_arg_ptr envp = { .ptr.native = __envp };
1783         return do_execveat_common(AT_FDCWD, filename, argv, envp, 0);
1784 }
...
1859 SYSCALL_DEFINE3(execve,
1860                 const char __user *, filename,
1861                 const char __user *const __user *, argv,
1862                 const char __user *const __user *, envp)
1863 {
1864         return do_execve(getname(filename), argv, envp);
1865 }
```
因為還有與`execveat`之類的大同小異的系統呼叫共用底層核心實作，所以一進核心（嚴格來說是一進入系統呼叫函式）就立刻轉到`do_execve`函式，這樣的處理手法我們已經看過很多次了。到了`do_execve`時其實也沒有多做什麼事，就是把字串的格式轉換成`struct user_arg_ptr`，這個型別宣告如下（同樣在`fs/exec.c`中）：
```
 398 struct user_arg_ptr {
 399 #ifdef CONFIG_COMPAT
 400         bool is_compat;
 401 #endif
 402         union {
 403                 const char __user *const __user *native;
 404 #ifdef CONFIG_COMPAT
 405                 const compat_uptr_t __user *compat;
 406 #endif
 407         } ptr;
 408 };
```

然後是`do_execveat_common`的呼叫，這就是一個長度超過一百行的函式了。
```
1633 static int do_execveat_common(int fd, struct filename *filename,
1634                               struct user_arg_ptr argv,
1635                               struct user_arg_ptr envp,
1636                               int flags)
1637 {
1638         char *pathbuf = NULL;
1639         struct linux_binprm *bprm;
1640         struct file *file;
1641         struct files_struct *displaced;
1642         int retval;
1643 
1644         if (IS_ERR(filename))
1645                 return PTR_ERR(filename);
...
```
第一個`fd`的參數從這條執行路徑看來傳入的是`AT_FDCWD`，這是支援`openat`或`execveat`之類的系統呼叫可以把參考點設在當前工作目錄之外的所在的時候使用的。`flags`參數設為0也是沒有任何額外的要求的意思。除此之外的中間三個參數沒什麼特別的處理。
```
1647         /*
1648          * 我們將超過RLIMIT_NPROC（程序數超過限制）的實際錯誤處理從
1649          * set*uid()系列呼叫搬到execve()裡面，因為實在是太多寫得很爛的程式
1650          * 不檢查setuid()的回傳值了。我們於此還重新檢查NPROC是否超過。
1652          */
1653         if ((current->flags & PF_NPROC_EXCEEDED) &&
1654             atomic_read(&current_user()->processes) > rlimit(RLIMIT_NPROC)) {
1655                 retval = -EAGAIN;
1656                 goto out_ret;
1657         }
1658 
1659         /* 我們仍然在限制之下，所以不會希望使得後續的execve呼失敗。*/
1661         current->flags &= ~PF_NPROC_EXCEEDED;
```
註解和程式碼充足的解釋了這個片段，然後，
```
1663         retval = unshare_files(&displaced);
1664         if (retval)
1665                 goto out_ret;
1666 
1667         retval = -ENOMEM;
1668         bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
1669         if (!bprm)
1670                 goto out_files;
1671 
1672         retval = prepare_bprm_creds(bprm);
1673         if (retval)
1674                 goto out_free;
```
`unshare_files`的呼叫在`kernel/fork.c`中，在這裡則是用來隔離父子程序之間的檔案描述子table。1667行之後預設了回傳錯誤的`-ENOMEM`，也就是說這裡要有配置記憶體操作了，目標是一個**二進位程式結構（bprm）**。1672行進一步準備這個程式的權限相關結構。
```
1676         check_unsafe_exec(bprm);
1677         current->in_execve = 1;
1678 
1679         file = do_open_execat(fd, filename, flags);
1680         retval = PTR_ERR(file);
1681         if (IS_ERR(file))
1682                 goto out_unmark;
1683 
1684         sched_exec();
```
1676行檢視執行`bprm`這件事情是否足夠安全，`check_unsafe_exec`回傳值是`void`，因為相關的結果都存在`bprm->unsafe`這個成員變數之內。1677標註當前程序正在`execve`之內的狀態。然後終於開啟了由參數傳入的這個檔案。1684行的`sched_exec`是scheduler認為的大好時機，這時候這個程序還處在最小的記憶體/快取使用量的期間，可以做負載平衡的調整。接下來：
```
1686         bprm->file = file;
1687         if (fd == AT_FDCWD || filename->name[0] == '/') {
1688                 bprm->filename = filename->name;
1689         } else {
1690                 if (filename->name[0] == '\0')
1691                         pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d", fd);
1692                 else
1693                         pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d/%s",
1694                                             fd, filename->name);
1695                 if (!pathbuf) {
1696                         retval = -ENOMEM;
1697                         goto out_unmark;
1698                 }
1699                 /*
1700                  * Record that a name derived from an O_CLOEXEC fd will be
1701                  * inaccessible after exec. Relies on having exclusive access to
1702                  * current->files (due to unshare_files above).
1703                  */
1704                 if (close_on_exec(fd, rcu_dereference_raw(current->files->fdt)))
1705                         bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;
1706                 bprm->filename = pathbuf;
1707         }
1708         bprm->interp = bprm->filename;
```
1687行的判斷是要決定傳入參數是否是絕對路徑。若是的話則直接將`filename->name`設成`bprm`的內容。如果是其他的任何情況都表示是相對路徑。1690到1698的處理是在準備相對路徑的轉換，而這裡竟然是把他們轉成`/dev/fd/...`的內容！筆者看到這裡是嘆為觀止了，但仍然無法理解的是，有什麼使用情境會讓執行檔名的第一個字元是`\0`？又有什麼情況，會讓`/dev/fd/%d`是一個資料夾而非連結？是開啟資料夾的時候嗎？...暫且先按下不管。

1704行對那些開啟時給定`O_CLOEXEC`屬性的檔案，因為現在的核心就正在`execve`了，所以要把fd table處理一下。然後1706行指定`bprm`結構對應的檔名。最後將這個檔名再次賦值給`interp`成員。在`bprm`所屬的`struct linux_binprm`結構內容有這樣的描述（`include/linux/binfmt.h`）：
```
 40         const char * filename;  /* Name of binary as seen by procps */
 41         const char * interp;    /* Name of the binary really executed. Most
 42                                    of the time same as filename, but could be
 43                                    different for binfmt_{misc,script} */
```
大部分時候這兩者都相同，但可能在註解中提到的情況下不同。

接續下去，
```
1710         retval = bprm_mm_init(bprm);
1711         if (retval)
1712                 goto out_unmark;
1713 
1714         bprm->argc = count(argv, MAX_ARG_STRINGS);
1715         if ((retval = bprm->argc) < 0)
1716                 goto out;
1717 
1718         bprm->envc = count(envp, MAX_ARG_STRINGS);
1719         if ((retval = bprm->envc) < 0)
1720                 goto out;
```
`bprm_mm_init`配置這個執行檔的記憶體，但是因為許多資訊還未明朗，所以還不能真正給出stack和權限設定等等，但稍後馬上就會處理。`count`函數在這裡是給`user_arg_ptr`的一個helper函數，紀錄裡面到底有多少項。
```
1722         retval = prepare_binprm(bprm);
1723         if (retval < 0)
1724                 goto out;
1725 
1726         retval = copy_strings_kernel(1, &bprm->filename, bprm);
1727         if (retval < 0)
1728                 goto out;
1729 
1730         bprm->exec = bprm->p;
1731         retval = copy_strings(bprm->envc, envp, bprm);
1732         if (retval < 0)
1733                 goto out;
1734 
1735         retval = copy_strings(bprm->argc, argv, bprm);
1736         if (retval < 0)
1737                 goto out;
```
`prepare_binprm`正式將`bprm`結構設定好，然後根據指定的檔案載入ELF的標頭或是script的直譯器。後面的部份則是一堆複製字串的過程。其中`bprm->p`看起來是憑空冒出來的，其實在1710行的`bprm_mm_init`裡面最後會指定這個成員變數；根據結構體內的註解，這個`p`值代表的是記憶體區段的最上面。`copy_strings`內部會拿這個變數當作位置的參考，並依照已經複製的長度不斷修改這個變數。
```
1739         retval = exec_binprm(bprm);
1740         if (retval < 0)
1741                 goto out;
1742 
1743         /* execve succeeded */
1744         current->fs->in_exec = 0;
1745         current->in_execve = 0;
1746         acct_update_integrals(current);
1747         task_numa_free(current);
1748         free_bprm(bprm);
1749         kfree(pathbuf);
1750         putname(filename);
1751         if (displaced)
1752                 put_files_struct(displaced);
1753         return retval;
```
1744行之後就是一些收拾善後的程式碼了。不可誤會1739的`exec_binprm`是真正執行新程式的所在，實際上，這只是為了`proc`檔案系統做一些資料上的紀錄。至此，這個系統呼叫就結束了。那麼，真正執行程式的地方在哪裡？實際執行動態追蹤功能觀察發現，直到從`sys_execve`回傳了，執行的使用者空間程式都還沒開始跑，這是很正常的！因為這個程式應該要在使用者空間執行，在那之前該做的事情只是把context（記憶體、起始位址）處理好，然後在結束核心空間之後，讓程序之後被排程到時可以從新的程式、新的參數與新的環境變數開始執行。

---
### 結論

`execve`是一個至關重要的系統呼叫，因為它提供了一個**讓程序能夠執行指定的程式的功能**，同時也讓核心初學者能夠清楚看到，過往在使用者空間不曾意識到的分別，如今可以非常清楚地顯示：程序是一個方便用來做資源分割的單位，程式則是運行在上面的東西。但是一如過去的一些比較複雜的系統呼叫一樣，本日的筆記也有一些謎團。比方說：那些關於ELF檔的處理的功能在哪裡？按照推理，`execve`是否應該要處理一些暫存器的配置好讓流程回到使用者空間時可以開始新程式的執行？在此先行打住的原因並非礙於篇幅，而是這系列的系統呼叫探索在筆者的能力之下目前只能作到這個差強人意的地步，也只能留待之後再深入探索了。

還需持續努力！到完賽前剩下10個系統呼叫，筆者希望能夠介紹一些記憶體，一些跨行程通訊，還有一些網路。也許使用者管理和檔案管理方面就得割捨了...再看看吧。雖然未曾謀得任何讀者的建議，但是你們的關注都是筆者前進的動力。我們明天再會！

