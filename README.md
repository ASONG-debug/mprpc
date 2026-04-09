# mprpc

`mprpc` 是一个基于 `muduo + protobuf + ZooKeeper` 实现的轻量级分布式 RPC 框架练手项目，目标是从零搭建一条完整的 RPC 调用链，包括：

- 服务注册与发现
- 自定义二进制协议
- 同步远程调用
- 多线程网络收发
- 超时与错误处理
- 基础可观测性

当前仓库包含：

- `libmprpc`：RPC 框架静态库
- `provider`：服务端示例程序
- `consumer`：客户端示例程序

## 项目亮点

这版项目已经不再只是“能跑通 demo”，而是围绕健壮性和完整性做了一轮增强，核心点包括：

- 设计了带 `magic/version/message_type/request_id` 的版本化 RPC 帧协议
- 重做 TCP 报文收发，支持完整包发送、完整包接收和超时控制
- 对请求和响应统一封装为固定帧头 + metadata + body，避免原始实现中一次 `recv` 假设收全包的问题
- 基于 ZooKeeper 支持多实例服务节点注册，并在客户端实现轮询负载均衡
- 为调用链增加 `request_id`、耗时、成功失败计数等日志埋点
- 修正了部分资源管理和异常路径，降低消息对象泄漏与异常中断风险

如果你希望把它写进简历，这个项目可以定位为：

> 基于 muduo / protobuf / ZooKeeper 实现的轻量级分布式 RPC 框架，具备服务发现、版本化协议、轮询负载均衡、超时控制与调用级日志观测能力。

## 目录结构

```text
mprpc
├─ src/
│  ├─ include/
│  │  ├─ mprpcapplication.h
│  │  ├─ mprpcchanel.h
│  │  ├─ mprpccontroller.h
│  │  ├─ mprpcconfig.h
│  │  ├─ logger.h
│  │  ├─ rpcprovider.h
│  │  ├─ rpcprotocol.h
│  │  └─ zookeeperutil.h
│  ├─ mprpcapplication.cc
│  ├─ mprpcchanel.cc
│  ├─ mprpccontroller.cc
│  ├─ mprpcconfig.cc
│  ├─ logger.cc
│  ├─ rpcprovider.cc
│  ├─ rpcheader.proto
│  └─ zookeeperutil.cc
├─ example/
│  ├─ callee/
│  │  └─ userservice.cc
│  ├─ caller/
│  │  └─ calluserservice.cc
│  ├─ user.proto
│  ├─ user.pb.h
│  └─ user.pb.cc
├─ test/
│  └─ protobuf/
├─ autobuild.sh
└─ CMakeLists.txt
```

## 依赖环境

建议在 Linux 环境下构建和运行，至少准备：

- `g++`
- `cmake >= 3.0`
- `make`
- `protobuf`
- `protoc`
- `muduo`
- `zookeeper_mt`
- `pthread`

如果 `muduo`、`protobuf`、`zookeeper` 不在系统默认搜索路径中，需要自行调整 CMake 里的头文件和库路径。

## 构建方式

### 方式一：脚本构建

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

## 快速运行

### 1. 启动 ZooKeeper

确保 ZooKeeper 已经启动，例如：

```text
127.0.0.1:2181
```

### 2. 准备配置文件

例如创建 `example/test.conf`：

```conf
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

### 3. 启动服务提供者

```bash
./bin/provider -i example/test.conf
```

### 4. 启动服务消费者

```bash
./bin/consumer -i example/test.conf
```

## 多实例与轮询负载均衡

这版实现里，服务端会在 ZooKeeper 中按如下结构注册：

```text
/service_name/method_name/ip:port
```

例如：

```text
/UserServiceRpc/Login/127.0.0.1:8000
/UserServiceRpc/Login/127.0.0.1:8001
```

客户端在调用时会读取某个方法下的全部可用实例列表，并按轮询策略选择目标节点。

这意味着你可以同时启动多个 `provider` 实例，通过不同端口注册到 ZooKeeper，然后验证客户端是否会轮流打到不同节点。

## 协议设计

当前 RPC 通信协议采用：

```text
[fixed frame header][metadata][body]
```

其中固定帧头定义在 `src/include/rpcprotocol.h`，字段包括：

- `magic`
- `version`
- `message_type`
- `request_id`
- `metadata_size`
- `body_size`

### Request

- `metadata`：protobuf 序列化后的 `RpcHeader`
- `body`：业务请求 protobuf 数据

`RpcHeader` 中包含：

- `service_name`
- `method_name`
- `args_size`

### Response

- `metadata`：响应状态元信息，包含 `status_code` 和 `error_text`
- `body`：业务响应 protobuf 数据

这套协议解决了旧版本中的几个典型问题：

- 只发 4 字节 `header_size`，扩展性太差
- 缺少协议版本，不方便后续升级
- 没有 `request_id`，难以定位和追踪请求
- 响应缺少统一错误元信息

## 核心调用流程

### 客户端调用流程

1. 用户创建 protobuf 生成的 `Stub`
2. `Stub` 的方法调用进入 `MprpcChannel::CallMethod`
3. `MprpcChannel` 将业务请求序列化
4. 查询 ZooKeeper 获取当前方法的所有可用实例
5. 通过轮询选择一个实例地址
6. 按新协议封装请求帧并通过 TCP 发送
7. 完整接收响应帧，解析元信息与业务响应
8. 记录请求 ID、调用耗时、错误信息等日志

### 服务端处理流程

1. 用户业务类继承 protobuf 生成的 `xxxRpc`
2. 通过 `RpcProvider::NotifyService()` 发布服务
3. `RpcProvider::Run()` 注册服务到 ZooKeeper 并启动 muduo 服务器
4. 收到请求后按帧协议解析请求头、metadata 和 body
5. 根据 `service_name + method_name` 定位目标服务方法
6. 反序列化参数并调用本地业务方法
7. 把业务结果封装为统一响应帧回传给客户端
8. 记录请求总数、失败数、耗时等指标日志

## 可观测性

这版补了基础可观测性，主要包括：

- `request_id`
- 调用耗时 `cost_ms`
- 服务名 / 方法名
- provider 节点地址
- 服务端总请求数
- 服务端总响应数
- 服务端失败请求数

日志对排查这些问题会更有帮助：

- 请求是否发到了预期节点
- 哪个方法经常失败
- 哪类请求耗时偏高
- 多实例轮询是否生效

## 关键模块说明

### `MprpcChannel`

客户端调用通道，负责：

- 业务请求序列化
- ZooKeeper 服务发现
- 轮询负载均衡
- TCP 完整收发
- 超时控制
- 响应解析与错误上报

### `RpcProvider`

服务端 provider，负责：

- 服务注册
- 方法分发
- 请求帧解析
- 统一错误响应
- 请求级日志统计

### `ZkClient`

ZooKeeper 封装层，负责：

- 建立连接
- 创建服务节点
- 获取子节点列表
- 提供多实例发现能力

### `MprpcController`

对 protobuf `RpcController` 的简单封装，当前支持：

- 调用失败信息
- 超时配置
- 请求 ID 回填

## 已完成的健壮性增强

相比项目早期版本，这一版重点补了这些问题：

- 修复一次 `send/recv` 假设收全包的问题
- 增加连接和收发超时控制
- 给请求和响应增加版本化帧头
- 服务端增加统一错误返回，而不是只打印日志或直接断开
- 使用 `unique_ptr` 管理服务端消息对象生命周期
- 轮询多个 provider 实例，而不是只从 ZooKeeper 取单节点

## 当前仍可继续优化的方向

如果继续往“完整简历项目”推进，下一阶段建议优先做：

- 长连接与连接池
- 限流、熔断、重试策略
- 更细粒度的监控指标
- 单元测试与集成测试
- 压测与性能优化
- 配置校验与动态配置
- 更完整的 CMake 依赖管理

## 简历表述参考

可以把这个项目概括为：

> 基于 muduo / protobuf / ZooKeeper 实现轻量级分布式 RPC 框架，设计版本化二进制协议，重构 TCP 完整收发链路，支持多实例服务发现、轮询负载均衡、超时控制与请求级日志观测。

如果你想写得更偏工程化，也可以表述为：

> 独立实现 RPC 框架核心模块，包括协议设计、网络收发、服务注册发现、负载均衡与异常处理；通过引入 request_id、统一错误响应和调用耗时统计，提升了系统的可观测性和健壮性。
