### 前情提要

我們已有了基本的標準檔案操作、程序相關操作以及訊號處理操作在核心裡面的相關實作的概念。過程中我們大量使用`gdb`或`strace`這樣的工具來透視系統呼叫的運作，這些功能都大量仰賴`ptrace`這個系統呼叫。這也是本日的重點。

---
### `ptrace`介面

```
NAME
       ptrace - process trace

SYNOPSIS
       #include <sys/ptrace.h>

       long ptrace(enum __ptrace_request request, pid_t pid,
                   void *addr, void *data);
```
筆者簡單翻譯一下手冊的描述：`ptrace`系統呼叫使得一個**追蹤者**程序能夠觀察或控制另外一個**受追蹤者**程序的內容（如記憶體或暫存器）或執行流程。這主要用來實作除錯器與系統呼叫的追蹤。使用這個系統呼叫之後，被追蹤者必須**依附**（attach）於追蹤者程序。這樣的依附行為是以執行緒為單位，也就是說，一個多執行緒的程序的每一個執行緒都可以個別依附在不同的追蹤者程序上，或是在其他執行緒被除錯時維持自己的執行流程。因此，一個被追蹤者所代表的總是一個執行緒，而非一個多執行緒的程序。

從`ptrace`的型別來看，可以發現有一個`enum __ptrace_request`，這個結構被定義在`/usr/include/sys/ptrace.h`（大部分的發行版應該都在這個位置）之中，因為超過一百行，這裡就只節錄片段（註解內為筆者翻譯）：
```
 27 /* REQUEST 參數之型別 */
 28 enum __ptrace_request
 29 {
 30   /* 代表呼叫這個ptrace的程序應該被追蹤。
 31      這個程序所接收到的所有訊號都可以被其父程序處理，
 32      其父程序亦可使用ptrace發起其他請求。 */
 33   PTRACE_TRACEME = 0,
 34 #define PT_TRACE_ME PTRACE_TRACEME
 35 
 36   /* 回傳程序在text記憶體空間中的addr位置（第三個參數）的內容。 */
 37   PTRACE_PEEKTEXT = 1,
 38 #define PT_READ_I PTRACE_PEEKTEXT
...
```
還有許多請求參數，如`PTRACE_POKEXXXX`系列可以修改程序內記憶體的值、`PTRACE_XXXREGS`可以讀寫暫存器的值等。這都是深入監控一個程序的行為不可或缺的功能，`ptrace`便因此而存在。

---
### 範例

筆者提供一個使用範例，因為有點長，所以非重點的部份就省略。
```
  1 #include<unistd.h>
  2 #include<sys/ptrace.h>
  3 #include<sys/types.h>
  4 #include<sys/time.h>
  5 #include<sys/resource.h>
  6 #include<sys/wait.h>
  7 #include<stdio.h>
  8 #include<stdlib.h>
  9 #include<string.h>
 10 
 11 int main(){
 12 
 13         pid_t pid = fork();
 14 
 15         if(pid > 0){
...
```
這是一開始的部份，`fork`呼叫一次之後產生了一個子程序，先看子程序在做的事情：
```
 51         else{
 52                 int is_end = 0;
 53                 int count = 0;
 54                 char fmt[40] = "count = %d\n";
 55 
 56                 printf("Addresses to be poked:\n");
 57                 printf("\tis_end = %p\n", &is_end);
 58                 printf("\tcount = %p\n", &count);
 59                 printf("\tfmt[8] = %p\n", &fmt[8]);
 60                 printf("===\n");
 61 
 62                 ptrace(PTRACE_TRACEME, 0, NULL, NULL);
 63 
 64                 while(!is_end){
 65                         kill(getpid(), SIGSTOP);
 66                         count++;
 67                         fprintf(stderr, fmt, count);
 68                         sleep(1);
 69                 }
 70                 return 0;
 71         }
```
子程序先提供了自己的三個局部變數的位置輸出給使用者看，然後使用`PTRACE_TRACEME`表明自己是被追蹤者，接下來在一個預設的無窮迴圈之中不斷的執行65~68行的動作。身為追蹤者的父程序，當然就要對子程序的這些情況做些調整：
```
 20 
 21                 ptrace(PTRACE_ATTACH, pid, NULL, NULL);
 22                 while(waitpid(pid, &wstatus, 0)){
 23                         if(WIFEXITED(wstatus)){
 24                                 printf("The child exits\n");
 25                                 return 0;
 26                         }
...
```
這個部份是父程序使用`PTRACE_ATTACH`先成為一個追蹤者，然後跑一個基於`waitpid`的迴圈隨時監控子程序。第一個判斷區塊是子程序是否已經離開，這個我們之前就看過了，接著是：
```
 27                         else if(WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) == SIGSTOP){
 28                                 if(count == 4){
 29                                         printf("Enter the address of count:\n");
 30                                         scanf("%p", &addr);
 31                                         printf("Enter a number:\n");
 32                                         scanf("%d", &data);
 33                                         ptrace(PTRACE_POKEDATA, pid, addr, data);
 34                                 }
 35                                 else if(count == 8){
 36                                         printf("Enter the address of fmt[8]:\n");
 37                                         scanf("%p", &addr);
 38                                         ptrace(PTRACE_POKEDATA, pid, addr, 0x0000000a78257830);
 39                                 }
 40                                 else if(count == 12){
 41                                         printf("Enter the address of is_end:\n");
 42                                         scanf("%p", &addr);
 43                                         ptrace(PTRACE_POKEDATA, pid, addr, 1);
 44                                 }
 45                                 ptrace(PTRACE_CONT, pid, NULL, NULL);
 46                         }
 47 
 48                         count++;
```
這個基於`waitpid`的迴圈中有一個`count`變數不斷紀錄跑的次數。根據剛才的子程序，子程序的每個迴圈都會觸發一次`SIGSTOP`，因此會使得父緒進入這個判斷區塊中；區塊內則有基於`count`變數的狀態變換，分別是在等於4的時候去修改子程序迴圈內的`count`值；等於8的時候去修改子程序`fprintf`使用的格式字串；等於12的時候去修改子程序的迴圈變數`is_end`為1，讓他離開迴圈。過程中使用者必須根據子程序最一開始透露的訊息與父程序的`scanf`互動，以使得父程序的`POKEDATA`能夠偷偷寫入子程序的記憶體。

> 等到我們探索到pipe或是shm系列的跨行程通訊系統呼叫，就可以回頭來自動化這個過程了！

這個程序執行起來會有類似以下的結果：
```
[demo@linux 19-ptrace]$ ./a.out 
Addresses to be poked:
	is_end = 0x7ffc6b753080
	count = 0x7ffc6b75307c
	fmt[8] = 0x7ffc6b753058
===
count = 1
count = 2
count = 3
Enter the address of count:
0x7ffc6b75307c
Enter a number:
23
count = 24
count = 25
count = 26
count = 27
Enter the address of fmt[8]:
0x7ffc6b753058
count = 0x1c
count = 0x1d
count = 0x1e
count = 0x1f
Enter the address of is_end:
0x7ffc6b753080
count = 0x20
The child exits
```

---
### 追蹤`ptrace`

`ptrace`的本體位在`kernel/ptrace.c`中：
```
1078 SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr,
1079                 unsigned long, data)
1080 {
1081         struct task_struct *child;
1082         long ret;
1083 
1084         if (request == PTRACE_TRACEME) {
1085                 ret = ptrace_traceme();
1086                 if (!ret)
1087                         arch_ptrace_attach(current);
1088                 goto out;
1089         }
...
```
最一開始是處理最特殊的選項，也就是唯一從被追蹤程序自己發出的選項`PTRACE_TRACEME`。在`ptrace_traceme`函數以及後續的呼叫中，因為牽涉到這個程序結構的改動，所以必須有一個`struct task_struct`的寫入鎖保護。要寫入什麼呢？主要就是`parent`這個指向令一個程序的成員變數，將會從原本的`real_parent`改到追蹤者程序上。這是因為除了如前段範例中的`fork->ptrace`組合之外，`gdb`等除錯工具在強大的`ptrace`之上其實也可以把一些已經在運行的程序抓起來追蹤。如果一切符合預期的話，這個被追蹤程序就會被加入追蹤程序的觀察清單之中。然後進入`arch_ptrace_attach`呼叫，但是這個呼叫在x86_64環境被定義成一個不做事的巨集。

接下來的部份，即全部都是從追蹤者角度的執行流程了。首先要對傳入的`pid`做一番判讀：
```
1090 
1091         child = ptrace_get_task_struct(pid);
1092         if (IS_ERR(child)) {
1093                 ret = PTR_ERR(child);
1094                 goto out;
1095         }
1096 
```
引用了`ptrace_get_task_struct`這個呼叫，會將`pid`數字轉為程序的結構，過程中也會為該程序的**存取數**增加1，除此之外當然也包含錯誤判斷的部份，若是這個程序不存在會回傳`-ESRCH`。

再接下來則處理追蹤者宣告要追蹤的兩個指令，`PTRACE_ATTACH`（成功則停下被追蹤程序）及`PTRACE_SEIZE`（不停止被追蹤程序）：
```
1097         if (request == PTRACE_ATTACH || request == PTRACE_SEIZE) {
1098                 ret = ptrace_attach(child, request, addr, data);
1099                 /*
1100                  * 有些處理器架構必須在attach之後做紀錄
1102                  */
1103                 if (!ret)
1104                         arch_ptrace_attach(child);
1105                 goto out_put_task_struct;
1106         }
```
這個`ptrace_attach`做了許多雜事，檢查權限與狀態之類的性質，筆者在這裡打住不繼續深入。
```
1108         ret = ptrace_check_attach(child, request == PTRACE_KILL ||
1109                                   request == PTRACE_INTERRUPT); 
1110         if (ret < 0)
1111                 goto out_put_task_struct;
1112 
1113         ret = arch_ptrace(child, request, addr, data);
1114         if (ret || request != PTRACE_DETACH)
1115                 ptrace_unfreeze_traced(child);
...
```
1108行會檢查這個被追蹤程序是否已經準備好了，若是，則可以進入`arch_ptrace`，也就是與處理器架構相依的核心部份。筆者的環境在`arch/x86/kernel/ptrace.c`之中，這個檔案有一個switch case處理部份請求。筆者想要節錄的`PTRACE_POKEDATA`和`PTRACE_CONT`的部份，因為屬於不相依於處理器架構的部份，因此會回到`kernel/ptrace.c`的`ptrace_request`函數：
```
 840 int ptrace_request(struct task_struct *child, long request,
 841                    unsigned long addr, unsigned long data)
 842 {
 843         bool seized = child->ptrace & PT_SEIZED;
...
 854         case PTRACE_POKETEXT:
 855         case PTRACE_POKEDATA:
 856                 return generic_ptrace_pokedata(child, addr, data);
...
1020         case PTRACE_CONT:
1021                 return ptrace_resume(child, request, data);
...
```
`generic_ptrace_pokedata`函式將透過位在`mm/memory.c`的`access_process_vm`函式去存寫入到指定的位置；`ptrace_resume`函式則主要呼叫`wake_upXXX`系列函數以讓暫停的程序繼續開始。

---
### 結論

本日簡單看了`ptrace`的部份功能。歷史上來說，這個系統呼叫的ABI是個很不討喜的設計，但是後來大家也就慢慢忍受它直到現在，可見的未來也還看不出會被徹底拋棄的跡象。在最後也剛好帶到我們從來沒有看過的`mm`子目錄的部份，在之後一定會有機會介紹到的。

感謝各位讀者的閱讀，我們明天會挑戰另外一個大型系統呼叫：`execve`。無論如何，明天再會！
