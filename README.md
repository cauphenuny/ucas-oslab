# Project1

Usage:

在 `(main)` 状态输入 Task 名字或者批处理命令：`/batch (store | load | run)` 。

`store` 的批处理文件格式：按顺序写出程序，忽略非标识符字符。

e.g.

```
(main) 2048
...
(main) /batch store
(store-batch) gen | mul3 | add10 | sqr
(main) /batch run
...
```
