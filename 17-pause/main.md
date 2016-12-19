### 前情提要

終於來到了訊號處理系列的尾聲。程序對訊號的處理，端看這個訊號是否被阻攔（`sigprocmask`相關設定），以及訊號處理程序的設置（`sigaction`的註冊）。程序與程序之間也可以用`kill`指令傳訊號。本日為鐵人賽中期，來探索`pause`這個系統呼叫，作為中間喘口氣的同時也結束訊號處理篇章。

---
### 介紹

```
NAME
       pause - wait for signal

SYNOPSIS
       #include <unistd.h>

       int pause(void);
```
這是`pause(2)`手冊的開頭內容。一個程序若是呼叫了這個系統呼叫，則會停止直到**接受到訊號**且**訊號處理程序回傳**，才會繼續下去。一個簡單的範例如下：
```
  1 #include<stdio.h>                                                                                                     
  2 #include<unistd.h>
  3 #include<signal.h>
  4  
  5 int state = 0;
  6  
  7 void siguser1(int signal){
  8         state = 1;
  9         return;
 10 }
 11  
 12 int main(){
 13  
 14         struct sigaction sa;
 15         sa.sa_handler = &siguser1;
 16         sigemptyset (&sa.sa_mask);
 17         sa.sa_flags = 0;
 18  
 19         sigaction(SIGUSR1, &sa, NULL);
 20  
 21         printf("before the pause: %d\n", state);
 22         pause();
 23         printf("after the pause: %d\n", state);
 24 }
```
`pause`會無限期卡在那裡，直到一個來自外部的`SIGUSR1` signal，可以成功喚起`siguser1`的訊號處理，然後將執行流程還給這個程序，並且可以觀察到這個訊號對於整個程序的狀態的改變。

至於`pause`的程式碼，同樣在`kernel/signal.c`之中：
```
3498 SYSCALL_DEFINE0(pause)
3499 {                
3500         while (!signal_pending(current)) {
3501                 __set_current_state(TASK_INTERRUPTIBLE);                                                                              
3502                 schedule();
3503         }        
3504         return -ERESTARTNOHAND;
3505 }
```
其中，`signal_pending(current)`檢查該程序的`TIF_SIGPENDING`旗標，若是沒有的話就將當前程序設為可中斷的，然後透過`schedule`呼叫將執行權還給系統，反之則離開迴圈，並回傳`-ERESTARTNOHAND`。但是根據手冊，這個呼叫將回傳-1，這又是怎麼回事呢？

觀察上述使用者程式的系統呼叫，則可以看到：
```
...
write(1, "before the pause: 0\n", 20before the pause: 0
)   = 20
pause()                                 = ? ERESTARTNOHAND (To be restarted if no handler)
--- SIGUSR1 {si_signo=SIGUSR1, si_code=SI_USER, si_pid=4487, si_uid=1000} ---
rt_sigreturn({mask=[]})                 = -1 EINTR (Interrupted system call)
write(1, "after the pause: 1\n", 19after the pause: 1
)    = 19
...
```
這是因為訊號處理的時候，最後return的是`rt_sigreturn`的緣故。從手冊上可以看到，這個呼叫是為了處理從核心空間返回時，因為訊號的關係而必須加入的中間層，用來整理程序的context用。

---
### 結論

雖說到了訊號處理篇章的尾聲，但是這個部份因為牽涉到許多非同步事件，只從系統呼叫下手所能獲得的知識相當有限。筆者自己感到興趣卻尚未得到滿足的的部份有二：

* Ctrl+C的處理，究竟是怎麼讓核心知道要砍掉哪個程序？目前的猜測是終端機的讀取功能取得這個控制字元之後，經過類似session ID的審查得知當前程序，然後決定將之關閉。

* SIGSEGV的產生。一個違法的記憶體存取如何導這這個訊號產生？

接下來若是有機會的話，或許還可以回來探討這些謎團。接下來的數日筆者打算安排一些難以分類（卻絕不輕鬆！）的系統呼叫，先是`alarm`這種與時間和訊號同時相關的（畢竟應該沒有時間探討時間了），然後是`ptrace`這個我們其實一直都很依賴的系統呼叫，再來是`execve`這個真正產生程序生態的呼叫。然後筆者預計接下來探討一些記憶體相關的部份，再緊接著使用者相關的功能和網路的功能。期望能夠圓滿完賽的同時，也祝福所有鐵人一起挑戰自己的極限並推動社群正向成長。我們明日再會！

