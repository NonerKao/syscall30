### 前情提要

昨日的`kill`看到訊號在核心中的傳送，本日則來探討程序如何指定特定行為來回應特定的訊號。

---
### `sigaction`的用途

原本一個程序對於每一種訊號都有預設的行為，而`sigaction`就是在改變這些特定的行為。最容易觀察到的例子，在一個bash啟用的時候：
```
[demo@linux ~]$ strace /bin/bash
execve("/bin/bash", ["/bin/bash"], [/* 39 vars */]) = 0
...
rt_sigaction(SIGCHLD, {0x4439d0, [], SA_RESTORER|SA_RESTART, 0x7f71d690f0b0}, {SIG_DFL, [], SA_RESTORER|SA_RESTART, 0x7f71d690f0b0}, 8) = 0
...
rt_sigaction(SIGINT, {0x7f71d6eab2d0, [], SA_RESTORER, 0x7f71d690f0b0}, {0x45c560, [], SA_RESTORER, 0x7f71d690f0b0}, 8) = 0
rt_sigaction(SIGTERM, {0x7f71d6eab2d0, [], SA_RESTORER, 0x7f71d690f0b0}, {0x45c080, [], SA_RESTORER|SA_RESTART, 0x7f71d690f0b0}, 8) = 0
rt_sigaction(SIGHUP, {0x7f71d6eab2d0, [], SA_RESTORER, 0x7f71d690f0b0}, {0x45c810, [HUP INT ILL TRAP ABRT BUS FPE USR1 SEGV USR2 PIPE ALRM TERM XCPU XFSZ VTALRM SYS], SA_RESTORER, 0x7f71d690f0b0}, 8) = 0
rt_sigaction(SIGQUIT, {0x7f71d6eab2d0, [], SA_RESTORER, 0x7f71d690f0b0}, {SIG_IGN, [], SA_RESTORER, 0x7f71d690f0b0}, 8) = 0
...
```
從上述例子我們可以看到這個系統呼叫需要四個傳入參數，第一個是欲處理的訊號本身，第二個和第三個結構類似，他們都是用來代表一個**訊號處理**的結構，前者是新的，後者是原先的。以第一個列出的`SIGCHLD`為例，原本的是`SIG_DEL`，也就是預設值的意思，將被改寫到一個位在bash本身程式之中的一個位置，由0x4439d0該處的函式來處理這個訊號。

> 其中有一件有趣的事情不得不提，那就是因為bash及大部分的shell處理了`SIGINT`，所以在命令列輸入Ctrl+C的時候不會將shell中止掉。

在手冊中有詳述`sigaction`（慣例的稱呼法）以及`rt_sigaction`（我們看見Linux核心使用的系統呼叫）的差異。主要是因為有一個結構`struct sigset_t`在不同需求（即時性與否）下的大小不同，因此`rt_sigaction`需要第四個參數，我們從上面的例子可以看到這個值大多是8個位元組。

---
### 追蹤

同樣在`kernel/signal.c`這個長達3K行的原始碼檔案中，有兩個`rt_sigaction`的定義，分別在`CONFIG_COMPAT`的組態以及`CONFIG_OLD_RT_SIGACTION`的組態下成立。檢視了一下目前使用的核心，發現筆者用的是前者，所以以前者作為標的來追蹤。

```
3328 COMPAT_SYSCALL_DEFINE4(rt_sigaction, int, sig,
3329                 const struct compat_sigaction __user *, act,
3330                 struct compat_sigaction __user *, oact,
3331                 compat_size_t, sigsetsize)
3332 {       
3333         struct k_sigaction new_ka, old_ka;
3334         compat_sigset_t mask;
3335 #ifdef __ARCH_HAS_SA_RESTORER
3336         compat_uptr_t restorer;
3337 #endif  
3338         int ret;
3339         
3340         /* XXX: Don't preclude handling different sized sigset_t's.  */
3341         if (sigsetsize != sizeof(compat_sigset_t))
3342                 return -EINVAL;
...
```
標頭如同前段所描述的編排，其中帶有`__user`的兩個參數都是來自使用者空間的訊號處理結構。最一開始的程式碼的`XXX`標記雖然說「請勿排除不同大小的`sigset_t`」，但目前為止仍然只支援一種，也就是`compat_sigget_t`。另外，本系統呼叫中的核心角色`struct compat_sigaction`定義在`include/linux/compat.h`，也就是一個為了系統呼叫相容性而定義許多型別與巨集的標頭檔中：
```
137 struct compat_sigaction {                                                                                                               
138 #ifndef __ARCH_HAS_IRIX_SIGACTION
139         compat_uptr_t                   sa_handler;
140         compat_ulong_t                  sa_flags;
141 #else    
142         compat_uint_t                   sa_flags;
143         compat_uptr_t                   sa_handler;
144 #endif
145 #ifdef __ARCH_HAS_SA_RESTORER
146         compat_uptr_t                   sa_restorer;
147 #endif
148         compat_sigset_t                 sa_mask __packed;
149 };               
```

接下來的部份是：
```
3344         if (act) {      
3345                 compat_uptr_t handler;
3346                 ret = get_user(handler, &act->sa_handler);
3347                 new_ka.sa.sa_handler = compat_ptr(handler);
3348 #ifdef __ARCH_HAS_SA_RESTORER
3349                 ret |= get_user(restorer, &act->sa_restorer);
3350                 new_ka.sa.sa_restorer = compat_ptr(restorer);
3351 #endif                  
3352                 ret |= copy_from_user(&mask, &act->sa_mask, sizeof(mask));
3353                 ret |= get_user(new_ka.sa.sa_flags, &act->sa_flags);
3354                 if (ret)
3355                         return -EFAULT;
3356                 sigset_from_compat(&new_ka.sa.sa_mask, &mask);
3357         }               
```
若是有傳入新的訊號處理結構`act`，才會執行這個部份。`get_user`是一個巨集，會根據傳入的指標的型別決定該如何進行後續的從使用者空間複製的動作，最後也會呼叫到`copy_from_user`。接下來主要就是填滿型別為`struct k_sigaction`的變數`new_ka`，然後最後呼叫的`sigset_from_compat`函數作一些bit尺度的轉換。
```
3359         ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
3360         if (!ret && oact) { 
3361                 sigset_to_compat(&mask, &old_ka.sa.sa_mask);
3362                 ret = put_user(ptr_to_compat(old_ka.sa.sa_handler), 
3363                                &oact->sa_handler);
3364                 ret |= copy_to_user(&oact->sa_mask, &mask, sizeof(mask));
3365                 ret |= put_user(old_ka.sa.sa_flags, &oact->sa_flags);
3366 #ifdef __ARCH_HAS_SA_RESTORER
3367                 ret |= put_user(ptr_to_compat(old_ka.sa.sa_restorer),
3368                                 &oact->sa_restorer);
3369 #endif           
3370         }        
3371         return ret;
3372 }                
```
`do_sigaction`將排除對`SIGKILL`及`SIGSTOP`註冊管理程序的企圖，並且在當前程序的結構體中取出指定的訊號處理結構，指派到`restorer`中，留待之後有需要時回復，而將傳入的註冊程序存放到當前程序的結構之中。最後的判斷式則是在前述過程沒有錯誤且有傳入舊的訊號處理結構`oact`時執行。

---
### 結論

這是一個相對單純的系統呼叫。我們看到了一個訊號管理程序如何被註冊，以及在核心中的處理過程。我們明日再會！

