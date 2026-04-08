# mprpc

一个基于 `muduo + protobuf + ZooKeeper` 的轻量级分布式 RPC 框架示例项目。

仓库当前包含：

- 框架静态库 `libmprpc`
- 服务提供者示例 `provider`
- 服务调用者示例 `consumer`

项目当前更适合作为 RPC 框架练手和学习工程，核心链路已经具备：

- 基于 protobuf 的服务定义与序列化
- 基于 muduo 的服务端网络收发
- 基于 ZooKeeper 的服务注册与发现
- 基于自定义二进制协议的 RPC 请求封装

## 目录结构

```text
mprpc
├─ src/                  # 框架核心代码
│  ├─ include/           # 对外头文件
│  ├─ mprpcchanel.cc     # 客户端调用通道
│  ├─ rpcprovider.cc     # 服务端 provider
│  ├─ zookeeperutil.cc   # ZooKeeper 封装
│  └─ logger.cc          # 异步日志
├─ example/
│  ├─ callee/            # 服务端示例
│  ├─ caller/            # 客户端示例
│  └─ user.proto         # 示例服务定义
├─ test/
│  └─ protobuf/          # protobuf 序列化测试
├─ autobuild.sh          # 一键构建脚本
└─ CMakeLists.txt
```

## 依赖环境

建议在 Linux 环境下构建和运行，至少准备以下依赖：

- `g++`
- `cmake >= 3.0`
- `make`
- `protobuf`
- `muduo`
- `zookeeper_mt`
- `pthread`

如果 `protoc`、`muduo`、`zookeeper` 没有安装到系统默认搜索路径，需要自行调整 CMake 的头文件和库路径。

## 构建方式

### 方式一：使用脚本

项目根目录执行：

```bash
chmod +x autobuild.sh
./autobuild.sh
```

常用参数：

```bash
./autobuild.sh --clean
./autobuild.sh --release
./autobuild.sh -j 8
```

### 方式二：手动构建

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

构建完成后默认产物：

```text
lib/libmprpc.a
bin/provider
bin/consumer
```

## 运行示例

先确保 ZooKeeper 已启动，例如：

```text
127.0.0.1:2181
```

然后准备一个配置文件，例如 `example/test.conf`：

```conf
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

启动服务端：

```bash
./bin/provider -i example/test.conf
```

另开一个终端启动客户端：

```bash
./bin/consumer -i example/test.conf
```

## 调用流程

### 服务端

1. 业务类继承 protobuf 生成的 `xxxRpc` 服务基类。
2. 调用 `RpcProvider::NotifyService()` 发布服务。
3. `RpcProvider::Run()` 启动 muduo 服务端并把服务注册到 ZooKeeper。

### 客户端

1. 创建 protobuf 生成的 `Stub` 对象。
2. `Stub` 底层通过 `MprpcChannel` 统一发起 RPC 调用。
3. `MprpcChannel` 从 ZooKeeper 查询目标服务地址。
4. 组装请求报文，通过 TCP 发送到 provider。
5. provider 反序列化请求，调用本地业务方法，并返回 protobuf 响应。

## 协议格式

当前请求格式为：

```text
[header_size(4字节)][rpc_header_str][args_str]
```

其中：

- `header_size`：`RpcHeader` 序列化后的长度
- `rpc_header_str`：protobuf 序列化后的方法头
- `args_str`：protobuf 序列化后的业务请求参数

`RpcHeader` 中包含：

- `service_name`
- `method_name`
- `args_size`

## 当前已知局限

这个项目已经能跑通基础 RPC 流程，但如果继续完善，建议优先关注：

- TCP 粘包/拆包处理仍然比较简化
- 部分错误处理和资源释放还可以加强
- CMake 依赖管理目前较基础
- 示例代码更偏教学用途，距离生产可用还有距离

## 后续可继续优化的方向

- 完善网络报文边界处理
- 增加连接复用和超时控制
- 改进日志模块
- 增加单元测试和集成测试
- 优化 CMake 结构和依赖查找

## 说明

本项目适合用来学习这些主题：

- RPC 框架基本原理
- protobuf 服务定义与代码生成
- muduo 网络库的基本用法
- ZooKeeper 服务注册与发现
- C++ 服务端工程组织方式
