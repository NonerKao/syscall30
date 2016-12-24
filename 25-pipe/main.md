### 前情提要

記憶體管理篇告一段落，接下來在網路相關的系統呼叫之前，插播一個跨行程通訊用的呼叫：`pipe`（管線）。

---
### 

管線最常見的使用方式就是在shell環境之下使用'|'符號連接不同指令。這麼做的話，會使得左端的指令的標準輸出接到右端的指令的標準輸入。手冊是這麼寫的：
```
NAME
       pipe, pipe2 - create pipe

SYNOPSIS
       #include <unistd.h>

       int pipe(int pipefd[2]);

       #define _GNU_SOURCE             /* See feature_test_macr
os(7) */
       #include <fcntl.h>              /* Obtain O_* constant d
efinitions */
       #include <unistd.h>

       int pipe2(int pipefd[2], int flags);
```
說明手冊也包含了GNU的特殊feature的`pipe2`，這裡就不提而專注在`pipe`呼叫上面。乍看之下很簡陋，只配置兩個檔案描述子，但其實這正是管線奧妙之處。

成功回傳之後，會有一個唯讀的`pipefd[0]`和唯寫的`pipefd[1]`共兩個檔案描述子。雖然本系列的主題是系統呼叫，但是這裡要多花一點篇幅描述一下bash如何使用的例子。bash會有很多部份在解析命令列的語法，而當他開始打算執行命令列的時候會用`execute_command`這個函數，隨後呼叫一個`execute_command_internal`（在`execute_cmd.c`），這個函數預設會有`pipe_in = NO_PIPE; pipe_out = NO_PIPE`的參數輸入。發現有'|'符號涉入的時候，就會呼叫內含`fork`的`make_child`函數（在`jobs.c`）；創得兩個子程序之後，就會使用`pipe`系統呼叫先建立一個通道，然後在前一個程序關掉唯讀`[0]`的接口，後一個程序則關掉唯寫`[1]`的接口，如此一來左程序就可以透過這個管線傳資訊給右程序。

> 其實還有一個重點是，左程序的`pipefd[1]`必須對應到標準輸出的1，而右程序的`pipefd[0]`則必須對應到標準輸入的0。這個功能使用`dup*`系列的系統呼叫實現。系列文已經沒有空間可以介紹這個系統呼叫，這裡簡單描述，就是複製一個檔案述子的所有功能給另外一個檔案描述子。如果被複製的目標原先存在，則直接關閉。以管線的應用例子，就是左程序將`pipefd[1]的性質給予`標準輸出的1，而原本開啟終端機的1號描述子會被關掉，右程序方向相反以此類推。

---
### 範例程式


