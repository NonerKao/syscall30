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

在手冊中有詳述`sigaction`（慣例的稱呼法）以及`rt_sigaction`（我們看見Linux核心使用的系統呼叫）的差異。主要是因為有一個結構`struct sigset_t`在不同需求（即時性與否）下的大小不同，因此`rt_sigaction`需要第四個參數，我們從上面的例子可以看到這個值大多是8個位元組。

---
### 結論


