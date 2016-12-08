### 前情提要

之前介紹了讀、寫、開啟等檔案操作，並且使用過終端機和ext4作為檔案開啟實例進行簡單的動態追蹤。

---
### 關閉

```
NAME
       close - close a file descriptor

SYNOPSIS
       #include <unistd.h>

       int close(int fd);
```
有開就有關。透過`open`呼叫取得的檔案描述子，可以透過`close`予以關閉。這個系統呼叫同樣被定義在`fs/open.c`之中：
```
1110 SYSCALL_DEFINE1(close, unsigned int, fd)
1111 {
1112         int retval = __close_fd(current->files, fd);
1113 
1114         /* can't restart close syscall because file table entry was cleared */
1115         if (unlikely(retval == -ERESTARTSYS ||
1116                      retval == -ERESTARTNOINTR ||
1117                      retval == -ERESTARTNOHAND ||
1118                      retval == -ERESTART_RESTARTBLOCK))
1119                 retval = -EINTR;
1120 
1121         return retval;
1122 }
```
從昨天追蹤`open`的經驗，我們可以預期`close`的操作應該也會涉及到程序的`fstable`結構，從裡面將指定的fd移除之類的。這個程式碼區段佔據視線的很大部份是關於錯誤回傳的狀況，這四種狀況都定在`include/linux/errno.h`，是核心內部使用的flag，回傳的則是標準的`-EINTR`，代表這個關閉檔案描述子的過程中被中斷了。

主體的部份在於1112行的`__close_fd`呼叫。這個呼叫取得當前程序的開啟檔案狀態，以及傳入欲關閉的檔案描述子。這個意圖很明顯，所以我們繼續追蹤下去：
```
635 int __close_fd(struct files_struct *files, unsigned fd)
636 {        
637         struct file *file;
638         struct fdtable *fdt;
639 
640         spin_lock(&files->file_lock);
641         fdt = files_fdtable(files);
642         if (fd >= fdt->max_fds)
643                 goto out_unlock;
644         file = fdt->fd[fd];
645         if (!file)
646                 goto out_unlock;
647         rcu_assign_pointer(fdt->fd[fd], NULL);
648         __clear_close_on_exec(fd, fdt);
649         __put_unused_fd(files, fd);
650         spin_unlock(&files->file_lock);
651         return filp_close(file, files);
652 
653 out_unlock:
654         spin_unlock(&files->file_lock);
655         return -EBADF;
656 }
```
641行的`fdt`取得，之前在`read`的時候有看到過`struct fdtable *fdt = rcu_dereference_raw(files->fdt);`這樣的寫法，這裡的`files_fdtable`也是一個最後會呼叫到`rcu_dereference`系列的巨集，實際上處理的事情多了些檔案是否上鎖的判斷。647行透過rcu機制清空`fdt->fd[fd]`，至此，對這個程序而言，`fd`就已經不連結到任何真正存在的檔案了。接著到650行的unlock為止，這個程序對於開啟檔案的狀態修改告一段落，整個上鎖的區間就是一個critical section。

在這之前的部份相當於是核心迫使一個程序修正自己對於自己已開啟檔案的紀錄，而這之後的部份就是那個原本被使用的檔案如今要修正自己的資料結構。這個過程最後會回傳`filp_close`的執行結果，在同一個檔案之內，
```
1083 int filp_close(struct file *filp, fl_owner_t id)
1084 {
1085         int retval = 0;
1086 
1087         if (!file_count(filp)) {
1088                 printk(KERN_ERR "VFS: Close: file count is 0\n");
1089                 return 0;
1090         }
1091 
1092         if (filp->f_op->flush)
1093                 retval = filp->f_op->flush(filp, id);
1094 
1095         if (likely(!(filp->f_mode & FMODE_PATH))) {
1096                 dnotify_flush(filp, id);
1097                 locks_remove_posix(filp, id);
1098         }
1099         fput(filp);
1100         return retval;
1101 }
```
這裡也有物件導向的寫法應用在`filp`自帶的`flush`方法，但使用動態追蹤之後，無論是終端機或是ext4的關閉都不會觸發這個`flush`方法，因為這兩個系統都沒有定義這個方法，也許其他類型的裝置會比較需要這個方法的實作吧。最後，`FMODE_PATH`相關的判斷較可能發生，其內的`dnotify_flush`函數定義在`fs/notify/dnotify/dnotify.c`之中，註解明確地說**每一次有檔案要關閉時必須呼叫**。dnotify是一個知會目錄結構的機制。

---
### 結論

本文的內容比較貧乏一點，因為沒有繼續深究其他的底層機制。下一篇即將迎來通用檔案處理的最後一個系統呼叫`ioctl`，並將順便深入挖掘與終端機相關的部份。之後則是以`fork`為首的程序管理篇。我們明天再見！

