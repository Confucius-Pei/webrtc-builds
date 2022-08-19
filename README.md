# WebRTC 编译all in one

fork的项目，详细readme可以参考原项目  

为了简单，方便学习，把能删掉的都删掉了  

不需要clone项目，可以直接下载一个压缩包，解压后编译。  
压缩包有1.5g左右，源代码文件太多，解压缩很耗时。  

## 改动点

1. 把所有源代码都上传到了github，不需要去其他地方拉代码了
2. 源代码不会更新了，固定在了某个版本上，哪个版本我也没看.不过可以自己下载一个版本，覆盖本项目里面的
3. vs的路径写死了2019的安装路径，可以参考init-msenv脚本函数
4. 编译需要debuging tools for windows,具体安装可以google或百度
5. 只支持windows，其他平台删掉了

## 编译

``` sh
# Build latest WebRTC for current platform:
./build.sh

# To compile with x86 libraries you would run:
./build.sh -c x86
```

## Running tests

Once you have compiled the libraries you can run a quick compile test to ensure build integrity:

``` sh
./test/run_tests.sh out/webrtc-17657-02ba69d-linux-x64
```
