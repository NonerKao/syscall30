### 前情提要

延續前兩日的程序識別探討，本文要完成程序識別的二層級架構的的最高層，也就是共用一個終端機的程序共同所在的單位：session。

---
### session到底是什麼？


---
### `get/setsid`
[noner@heros 9-getxid-setxid]$ gcc sid.c -o sid
[noner@heros 9-getxid-setxid]$ ./sid
This process if of session 4746
Process 10400 if of session 4746
Process 10401 if of session 10401
[noner@heros 9-getxid-setxid]$ ps
  PID TTY          TIME CMD
 4746 pts/1    00:00:00 bash
10404 pts/1    00:00:00 ps

