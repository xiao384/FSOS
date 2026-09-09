# hello - 随 pt 终端内嵌的示例包
# 由 install hello 解包写入文件区后, 用 run hello 执行本文件。
print('hello from an installed package!')
print('1 + 1 =', 1 + 1)


def greet(who):
    print('你好, ' + who)


greet('FSOS')
