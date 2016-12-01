### 前言

系統呼叫（System Call）是應用程式所在之使用者空間（User Space），以及核心空間（Kernel Space）之中間介面，也是理解核心運作的一個良好切入點。在這次的鐵人挑戰中，筆者將採用由上而下（Top-Down）的視角，每天trace一個（或多個，如果高度相關且份量不多的時候）系統呼叫。

在這開宗明義的階段，筆者將說明關於前置知識、所使用的環境、工具、以及即將trace的幾個系統呼叫。最後於總論的開始，也提供一個暖身，介紹一些相對之下比較簡單的系統呼叫。

---
### 前置知識

作業系統存在的意義是提供一個抽象層，讓使用者能夠在一個方便的環境底下使用或開發應用程式，這個方便的環境就是User Space。

有別於Kernel Space的的程式碼必須直接面對CPU指令集及各式各樣的硬體，User Space的應用程式若需要與系統或硬體相關功能，就僅需依賴系統呼叫。而資訊系統發展至今，通常由函式庫（主要是C library）扮演第一線使用系統呼叫的角色，各應用程式與其他功能的函式庫存在其上間接地運用這些介面。

本文主要介紹的 Linux 系統呼叫，主要根基於Unix時代便已打下基礎的API設計。Linux並大體上遵守POSIX規範，這個為了可攜性設計的API規範廣義地定義了作業系統應提供的介面，於是大多數Unix-like作業系統在實作的選擇上會採用一對一的系統呼叫直接對應到這些API。舉例來說，輸出`Hello World`至虛擬終端機的`printf()`函數最終將會執行的`write()`呼叫，在Linux及FreeBSD中都有`write`系統呼叫直接與其函數對應。

因此，在這次三十天鐵人挑戰當中，筆者將挑選一些最常被使用的POSIX API，追蹤直接對應於他們的系統呼叫，其中大多數都是筆者未曾trace過的。希望這作為一段學習紀錄的同時，也能夠幫助對Kernel有興趣的讀者，一同透過追蹤系統呼叫介面的方式理解Linux核心中的核心功能。

---
### 使用環境

筆者的環境為x86_64的Arch Linux。在本系列文章中如有節錄核心程式碼，均出自執筆時Arch所使用的4.8.11版本，雖然大多追蹤的部份都與這些新的patch無關，僅是提供漫長核心開發歷史中的一個參考點。

過程中也可能會參照到man page，雖然在我們所需參考的部份（glibc、Linux Programmer's Manual等）都已臻穩定，但筆者仍於此註明環境使用的諸項套件版本為：
* `linux 4.8.4-1`
* `glibc 2.24-2`
* `man-pages 4.08-1`

---
### 工具

觀察系統呼叫的必備的工具是strace指令，在各Linux發行版中幾乎都有套件可供下載。`strace`將欲執行/觀察之應用程式及其參數作為參數傳入，並以觀察者身份（`gdb`等程式亦如此）在該應用程式執行至系統呼叫時，在標準錯誤（`stderr`）印出所執行的系統呼叫，如有指定詳細參數給予`strace`，則可以連詳細的呼叫內容都展示出來。順帶一提，由於許多Linux系統呼叫幾乎直接與POSIX標準的API對應，`ltrace`作為一個函式庫呼叫的輔助工具也是相當方便，在本文最後的暖身一節會有所著墨。

在追蹤程式碼方面，最好能夠動態、靜態雙管齊下。筆者使用的靜態trace核心程式碼的工具是`vim`與`cscope`的組合，當然偶爾也需要手動的`grep`一下；而動態工具則是使用qemu運行允許Debug模式的VM，再利用`gdb`進行遠端追蹤。相關的資源請參照各工具的官網或技術論壇。

另外，由於筆者對於作業系統的歷史認識不深，若要考證系統呼叫的淵源，必然會有許多缺漏之處，因此主要參考[Open Group現在維護的POSIX標準](http://pubs.opengroup.org/onlinepubs/9699919799/functions/contents.html)所提供的介面，而不作除了Linux的系統呼叫實作之外的介紹或評論。

---
### 目標系統呼叫

挑選目標系統呼叫的主要原則是常用與否，粗略列舉並分類如下：
- 通用之抽象功能：`open, close, read, write, ioctl, ...`
- 程序管理：`execve, fork, clone, exit, wait...`
- 網路通訊：`socket, bind, listen, connect, accept, sendmsg, recvmsg...`
- 檔案管理及操作：`stat, lseek, access, ...`
- 跨行程通訊：`pipe, shmget, shmctl, ...`
- 記憶體管理：`mmap, mprotect, brk, ...`
- 訊號管理：`kill, rt_sigaction, rt_sigprocmask, rt_sigreturn, ...`
- 時間相關：`nanosleep, gettimeofday, ...`
- 其它（還有非常多）

光是指出這幾個分類，所列舉的系統呼叫數量就已經超過30個了。筆者此時尚未決定應該以怎麼樣的順序來呈現，所以目前就以涵蓋這些常用呼叫的追蹤與分析為目標進行寫作。

---
### 暖身！來trace個`uname`

說這麼多，不如來暖身一下吧！開場的系統呼叫，筆者選擇`uname`，因為這是許多人在初次使用類Unix平台時會使用的功能，這個功能將會取得作業系統的名稱、版本號、CPU架構等資訊。儘管可能多數讀者的經驗都限定在Linux或特定的Unix工作站中，這個指令用來取得當前環境的核心版本號碼也是相當實用的。

`uname`在POSIX標準中出現於兩處，一個是作為工具程式指令（utility），令一個則是作為API（標準規定須提供於`sys/utsname.h`中）可供程式呼叫；顯然，`uname`指令的內部實作會理所當然地引用`uname()`函數，這點可以由`ltrace`驗證`uname`的執行期行為。而`uname()`的實作直接引用了Linux的`uname`系統呼叫。

依據目錄的分配大概可以猜想到，`uname`這樣一個取得系統資訊的系統呼叫，應該會在kernel資料夾中。而在kernel資料夾內，可以很直覺地看到一個`utsname.c`，懷抱著「大概就是這個了吧！」的心情進去閱讀會發現，裡面註冊了一個`procfs`用的初始化函數`utsns_install`，`ns`是namespace的縮寫，裡面大多都是與UTS namespace（UTS是Unix Time Sharing的簡寫）相關的內容，卻與我們想要找的`uname`系統呼叫沒什麼關聯。

直覺不管用的時候可以使用`grep`大法，以系統呼叫使用的巨集`SYSCALL_DEFINE`和`uname`作為關鍵字，可以找到其實除了`uname`之外，還有兄弟系統呼叫`olduname`和`newuname`被一同定義在kernel/sys.c之中。先不考慮其他二者，`uname`的宣告如下：
```
SYSCALL_DEFINE1(uname, struct old_utsname __user *, name)
{
        int error = 0;

        if (!name)
                return -EFAULT;

        down_read(&uts_sem);
        if (copy_to_user(name, utsname(), sizeof(*name)))
                error = -EFAULT;
        up_read(&uts_sem);

        if (!error && override_release(name->release, sizeof(name->release)))
                error = -EFAULT;
        if (!error && override_architecture(name))
                error = -EFAULT;
        return error;
}
```

由巨集的`SYSCALL_DEFINE1`可以知道，`uname`這個call會帶一個參數傳入，這部份與API一模一樣。也就是說，`name`是一個`old_utsname`的結構體的指標。因此一開始判斷這指標是否有效，若是無效則直接回傳`-EFAULT`，根據`error-base.h`標頭檔的定義，這是Bad Address的意思，也就是無效的指標。

通過這個測試之後，就是要準備將核心內的相關資訊複製到使用者空間去的`copy_to_user`一段。前後透過`uts_sem`旗標專門保護系統資訊的讀取，然後呼叫`utsname()`函數，而這是`include/linux/utsname.h`裡面的一個inline函數：
```
static inline struct new_utsname *utsname(void)
{
        return &current->nsproxy->uts_ns->name;
}
```

這就有趣了，`uname`這樣簡單的系統呼叫，難道不是該直接把結構體對應的內容填滿就好了嗎？核心版本3.0更早之前或許是如此，但在那之後加入了UTS namespace功能，使得Linux核心允許不同的程序可以對自己所處的系統有不同的看法，更嚴謹地說，就是作業系統層級虛擬化（OS-level Virtualization），或是更有名一點的說法：容器（Container）這樣的機制。一般會用在更改容器內部的Hostname或Domainname。

抽絲剝繭，`current`是當前呼叫uname的程序；`nsproxy`是一個統合各個namespace的結構體，通常子程序會繼承父程序的設定，從而有一樣的命名空間；`uts_ns`是整體中特指UTS namespace的部份，其型別為定義在`include/linux/utsname.h`的`struct uts_namespace`；
```
struct uts_namespace {
        struct kref kref;
        struct new_utsname name;
        struct user_namespace *user_ns;
        struct ns_common ns;
};
extern struct uts_namespace init_uts_ns;
```
最後的`name`，即是`struct new_utsname`的結構體，
```
#define __NEW_UTS_LEN 64

struct old_utsname { 
        char sysname[65];
        char nodename[65]; 
        char release[65];
        char version[65];
        char machine[65];
};

struct new_utsname {
        char sysname[__NEW_UTS_LEN + 1];
        char nodename[__NEW_UTS_LEN + 1];
        char release[__NEW_UTS_LEN + 1];
        char version[__NEW_UTS_LEN + 1];
        char machine[__NEW_UTS_LEN + 1];
        char domainname[__NEW_UTS_LEN + 1];
};      
```
我們可以看到Linux目前在這兩個新舊的定義差別在於新版的新增了domainname項目。之前提到傳入的`name`屬於`struct old_utsname`，`utsname()`回傳的卻是`struct new_utsname`，這樣豈不是會有多餘的記憶體寫入非法的空間嗎？幸好`copy_to_user`的設計還帶有第三個參數，負責規定所應複製的長度大小為何，而我們可以很清楚的看到這是以舊版本為基準的寫法，因為這裡傳入了`sizeof(*name)`的緣故。這個`copy_to_user`的呼叫若是成功，則`name`指標應該就已經取得了需要的核心資訊。

最後的部份還有兩個條件判斷，分別就release版本號對於呼叫程序是否需要修改，以及architecture的相容性是否需要修改而判斷。這兩個呼叫都涉及到一個程序的`personality`判斷，那也是一個系統呼叫相關的項目。但通常情況不會觸發這兩個判斷而回傳`-EFAULT`。

---
### 結論

`uname`系統呼叫本身不甚複雜，其抽象意涵也屬於很容易想像的那一種。但仍然存有最後的疑問，那就是：`utsname`這個inline函數那一長串的最後的`name`又是哪裡來的？

這就是`kernel/utsname.c`派上用場的時候了。在初始化階段，`uts_init_ns`變數就已經被指定好為預設的uname內容，而若有其餘容器空間存在，則還會有utsns生成的inode。相關的訊息可以在`/proc/PID/ns/`裡面的`uts`一項觀察得到。最後存取到的`name`結構，會存有該程序對系統應當有的認識，無論這個認識是預設且原生的或是虛擬化的。

作為一個暖身用系統呼叫，打起來也不知不覺到了數千字之譜，希望這個挑戰能夠帶給許多人不同面向的幫助，也希望這些分享能夠多少讓需要的人看見。使用者空間的範例程式碼連同文章將一起分享到[筆者的Github帳號](https://github.com/NonerKao/syscall30)，感謝所有讀者的指教，我們明天再見！
