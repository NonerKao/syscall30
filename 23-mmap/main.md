
---
### 追蹤

根據手冊的`NOTE`一節，C函式庫的wrapper與系統呼叫的介面有一點點不同在於`addr`

一開始費了些手腳發現`mmap`在`arch/x86/kernel/sys_x86_64.c`之中，
```
 86 SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
 87                 unsigned long, prot, unsigned long, flags,
 88                 unsigned long, fd, unsigned long, off)
 89 {                
 90         long error;
 91         error = -EINVAL;
 92         if (off & ~PAGE_MASK)
 93                 goto out;
 94                  
 95         error = sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);                                                           
 96 out:             
 97         return error;
 98 }                
```

```
1305 SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
1306                 unsigned long, prot, unsigned long, flags,
1307                 unsigned long, fd, unsigned long, pgoff)
1308 {               
1309         struct file *file = NULL;
1310         unsigned long retval;
1311                 
1312         if (!(flags & MAP_ANONYMOUS)) {
1313                 audit_mmap_fd(fd, flags);
1314                 file = fget(fd);
1315                 if (!file)
1316                         return -EBADF;
1317                 if (is_file_hugepages(file))
1318                         len = ALIGN(len, huge_page_size(hstate_file(file)));
1319                 retval = -EINVAL;
1320                 if (unlikely(flags & MAP_HUGETLB && !is_file_hugepages(file)))
1321                         goto out_fput;
1322         } else if (flags & MAP_HUGETLB) {
```
