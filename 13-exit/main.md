### 前情提要

昨日非常簡略地帶過`clone`呼叫，在程序管理篇章的最後，介紹中止程序，也就是`exit`相關的系統呼叫。

---
### `exit`和`exit_group`

使用`strace -f`模式（能觀察fork出的子程序及執行緒），可以得到下面片段：
```
[pid 17662] exit(0)                     = ?
[pid 17662] +++ exited with 0 +++
[pid 17661] <... futex resumed> )       = 0
[pid 17661] exit_group(0)               = ?
[pid 17661] +++ exited with 0 +++
<... wait4 resumed> [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 17661
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_EXITED, si_pid=17661, si_uid=1001, si_status=0, si_utime=0, si_stime=0} ---
exit_group(0)                           = ?
```
17662是子程序產生的執行緒，`pthread_join`內部採用`futex`等待之，結束的手法是`exit`；17661則是子程序本身，使用的結束呼叫則是`exit_group`這個系統呼叫。這兩個呼叫都出現在`kernel/exit.c`中，我們可以分別看看他們的內容：

```

```

---
### 結論

