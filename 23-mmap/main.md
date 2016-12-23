### 前情提要

程序隨時都要使用到虛擬記憶體，從配置到使用的過程，我們都需要許多的機制。其中我們已經介紹了動態配置記憶體的核心功能`brk，以及設置存取權限給虛擬記憶體區段的`mprotect`。本日的主角則是`mmap`這個系統呼叫。

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
也就是將檔案或是裝置的部份**映射**到記憶體上面。
