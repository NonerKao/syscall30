### 前情提要

訊息處理作為一個章節，我們看過了`kill`、`sigprocmask`、`sigaction`以及`pause`，對於訊息的許多機制仍然不清楚，但學習就是一步一步走下去的過程。筆者將接下來的幾個系統呼叫定位為難以明確分類的過渡期主題，本日則延續訊息處理的脈絡，來看看`alarm`的機制。

---
### 臨時補充：關於昨日的`pause`

為了實作`pause`的使用者空間範例程式，筆者本來打算想要展示來自子程序的SIGCHLD的影響。但是後來發現，Linux環境下對`SIGCHLD`的訊號處理的預設策略是忽略，所以從`pause`回傳的條件就無法成立了。但只要修改這個預設策略並提供一個訊號處理函式就可以排除這個狀況，只需要引用前幾日介紹的`sigaction`呼叫即可。新修改的範例程式於此：
```
#include<stdio.h>
#include<unistd.h>
#include<signal.h>

int state = 0;

void siguser1(int signal){
	state = 1;
	return;
}

int main(){

	struct sigaction sa;
	sa.sa_handler = &siguser1;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGCHLD, &sa, NULL);

	if(!fork()){
		sleep(3);
		exit(0);
	}

	printf("before the pause: %d\n", state);
	pause();
	printf("after the pause: %d\n", state);

}
```
想必讀者都注意到了這份程式違反我們在程序管理篇的`wait`篇章提到的重點，那就是父程序必須負起責任去呼叫一個`wait`家族函數以接收子程序的離開，否則子程序將成為殭屍狀態。是的，這表示這個程式**不應該在任何正式的場合被使用**，我們僅僅是使用這個範例來展示`pause`將會回應子程序結束的`SIGCHLD`。

---
### `alarm`介紹

讓我們回到`alarm`上面吧。這是`alarm(2)`的開頭定義：
```
NAME
       alarm - set an alarm clock for delivery of a signal

SYNOPSIS
       #include <unistd.h>

       unsigned int alarm(unsigned int seconds);

```

這個系統呼叫可以倒數計時`seconds`秒的時間，到了時就傳一個`SIGALRM`給自己。為了檢驗這個特徵，最簡單的方式是修改昨日的範例程式如下：
```
  1 #include<stdio.h>
  2 #include<unistd.h>
  3 #include<signal.h>
  4          
  5 int state = 0;
  6          
  7 void sigfunc(int signal){
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
 18         sigaction(SIGALRM, &sa, NULL);
 19          
 20         printf("before the pause: %d\n", state);
 21         alarm(3);
 22         pause();                                                                                                                         
 23         printf("after the pause: %d\n", state);
 24 }        
```
22行的`pause`呼叫一樣會卡住這個程序的運行，但不會太久，3秒之後，由`alarm`預約好的**訊號鬧鐘**即會發出`SIGALRM`。這個訊號會被`sigfunc`處理掉，然後回到原本的執行秩序。值得一提的是，`SIGALRM`訊號的預設處理方式是會強制離開的，所以這裡要是沒有註冊一個訊號處理程序，就會看不到第二次的訊息印出。

---
### 追蹤`alarm`

在`kernel/time/timer.c`之中：
```
1685 SYSCALL_DEFINE1(alarm, unsigned int, seconds)
1686 {               
1687         return alarm_setitimer(seconds);                                                                                                
1688 }               
```
`alarm_setitimer`在`kernel/timer/itimer.c`中，
```
253 unsigned int alarm_setitimer(unsigned int seconds)
254 {       
255         struct itimerval it_new, it_old;
256         
257 #if BITS_PER_LONG < 64
258         if (seconds > INT_MAX)
259                 seconds = INT_MAX;
260 #endif  
261         it_new.it_value.tv_sec = seconds;
262         it_new.it_value.tv_usec = 0;
263         it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
264         
265         do_setitimer(ITIMER_REAL, &it_new, &it_old);
266         
267         /*
268          * We can't return 0 if we have an alarm pending ...  And we'd
269          * better return too much than too little anyway
270          */
271         if ((!it_old.it_value.tv_sec && it_old.it_value.tv_usec) ||
272               it_old.it_value.tv_usec >= 500000)                                                                                          
273                 it_old.it_value.tv_sec++;
274         
275         return it_old.it_value.tv_sec;
276 }       
```
這個部份的核心是`do_setitimer`呼叫，在這之前是給予它一個`struct itimerval`的準備工程，也就是一個精準到微秒等級的結構；在這之後則是檢查回傳的`it_old`結構，這個結構代表可能存在的、未解決的alarm。最後則回傳`it_old`內的一個秒數，如果這個東西不存在當然就會回傳0，也就是在手冊裡描述的，**若是沒有先前排程的alarm則回傳0，否則回傳那個alarm被執行前的秒數**。

> `do_setitimer`是`alarm`、`setitimer`與`getitimer`等時間相關系統呼叫共用的一個核心函數，手冊`setitimer(2)`中也解釋了相關資料結構與不同模式的計時器的用法。

我們節錄這個範例程式會用到的`do_setitimer`的部份：
```
190 int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
191 {      
...
203         switch (which) {
204         case ITIMER_REAL:
205 again:  
206                 spin_lock_irq(&tsk->sighand->siglock);
207                 timer = &tsk->signal->real_timer;
208                 if (ovalue) {
209                         ovalue->it_value = itimer_get_remtime(timer);
210                         ovalue->it_interval
211                                 = ktime_to_timeval(tsk->signal->it_real_incr);
212                 }
213                 /* We are sharing ->siglock with it_real_fn() */
214                 if (hrtimer_try_to_cancel(timer) < 0) {
215                         spin_unlock_irq(&tsk->sighand->siglock);
216                         goto again;
217                 }
218                 expires = timeval_to_ktime(value->it_value);
219                 if (expires.tv64 != 0) {
220                         tsk->signal->it_real_incr =
221                                 timeval_to_ktime(value->it_interval);
222                         hrtimer_start(timer, expires, HRTIMER_MODE_REL);
223                 } else
224                         tsk->signal->it_real_incr.tv64 = 0;
225         
226                 trace_itimer_state(ITIMER_REAL, value, 0);
227                 spin_unlock_irq(&tsk->sighand->siglock);
228                 break;
...
```
其中，`ovalue`相關的資訊取得是為了判斷是否有未完成的計時器。由於`alarm`的作為必須能夠**取消所有先前設定的計時器**，所以214行會試圖取消先前的timer並且不斷嘗試。218行之後則是開始處理輸入進來的計時器需求，最後在`hrtimer_start`開始這個計時。

在筆者的核心組態下，這些計時器最後都會連結到**高解析度計時器**（High-Resolution Timer）的功能去，所以這裡才會有`hrtimer_start`之類的函數。在這個函數一連串的呼叫過程中，最後會將這個`timer`加入到一個專門紀錄計時器的**紅黑樹**中，開始計時。

至於怎麼計時呢？這是另外一個面向的問題。在筆者的環境裡，計時器之所以能夠計時，是因為硬體有支援，每個單位時間就會給核心一個硬體的中斷，然後這個中斷會被傳到`kernel/timer/hrtimer.c`裡面的`hrtimer_interrupt`函數來處理，這個函數呼叫到最後，如果發現時間已經到了，就會執行到上面程式碼區段列出的213行提到的`it_real_fn`函數。這個函數在每次有fork行為出現的時候，紀錄在程序的結構裡面當作callback。它實際的作為是：
```
121 enum hrtimer_restart it_real_fn(struct hrtimer *timer)                                                                                    
122 {
123         struct signal_struct *sig =
124                 container_of(timer, struct signal_struct, real_timer);
125  
126         trace_itimer_expire(ITIMER_REAL, sig->leader_pid, 0);
127         kill_pid_info(SIGALRM, SEND_SIG_PRIV, sig->leader_pid);
128  
129         return HRTIMER_NORESTART;
130 }
```
也就是說，這就是呼叫`alarm`的程序之所以會在指定時間之後收到`SIGALRM`的原因。

---
### 結論

時間處理的部份處理的有些倉促，但至少透過`alarm`系統呼叫的探討，瀏覽了一些相關機制的運行。接下來要陸續挑戰`ptrace`以及`execve`等大型系統呼叫，我們明天再會！
