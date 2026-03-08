## Context

当前仓库根目录下已有 `bot-node-sdk` 作为 TypeScript 参考实现，包含三个关键抽象：
- `bot.ts` 暴露 SDK 的统一创建入口。
- `openapi/v1/openapi.ts` 负责版本注册、配置持有、鉴权头拼装和基础请求派发。
- `client/session/session.ts`、`client/client.ts`、`client/websocket/websocket.ts` 负责 WebSocket 建连、心跳、重连和事件派发。

本次变更目标不是一次性覆盖 Node SDK 的全部业务 API，而是先在 `src/` 下落出一个可扩展的 C++ SDK 骨架，使 HTTP/OpenAPI 与 WebSocket 两条主链路具备稳定边界，并为后续补充频道、消息、成员、私信等模块提供统一宿主。

约束包括：
- 代码必须放在 `src/` 目录下。
- 设计要尽量保留 Node SDK 的使用心智，例如统一配置对象、版本化 OpenAPI 工厂和 WebSocket 客户端入口。
- C++ 需要显式处理资源生命周期、线程安全、异步回调与错误模型，不能简单照搬 TS 运行时语义。

## Goals / Non-Goals

**Goals:**
- 定义 `src/` 下的 C++ SDK 目录布局和模块边界。
- 定义统一配置、鉴权、User-Agent、基础 URL 与请求封装方式。
- 定义版本化 OpenAPI 聚合客户端，使业务 API 模块可以按版本扩展。
- 定义 WebSocket 会话管理、心跳、断线重连和事件分发的基础行为。
- 让后续实现阶段可以按模块逐步交付，而不需要重构整体骨架。

**Non-Goals:**
- 本次设计不要求一次性实现 Node SDK 中所有细分 API。
- 本次设计不要求绑定某一个最终网络库的所有细节实现。
- 本次设计不覆盖示例程序、发布打包、CI/CD 或跨平台安装脚本的完整方案。
- 本次设计不定义业务侧机器人处理逻辑，只定义 SDK 能力边界。

## Decisions

### 1. 采用“公共类型 + transport + openapi + websocket + facade”分层
将 `src/` 拆分为以下层次：
- `common/`：配置、错误、常量、工具函数。
- `transport/`：HTTP 请求器、WebSocket 适配器、JSON 序列化抽象。
- `openapi/`：版本注册、基础客户端、各业务 API 模块。
- `websocket/`：会话状态、网关协议、事件分发、重连控制。
- `sdk/`：对外暴露的创建入口与统一 facade。

这样可以对应 Node SDK 的现有职责划分，同时避免 C++ 代码把请求、协议和业务模块耦合在同一个类里。

备选方案是只做单层 `BotClient` 大类，集中处理 HTTP 和 WebSocket。该方案实现更快，但会让后续新增 API 模块和测试替身变得困难，因此不采用。

### 2. OpenAPI 采用版本注册表 + 聚合客户端
参考 `bot-node-sdk/src/bot.ts` 与 `openapi/openapi.ts` 的模式，C++ 版本也保留：
- 一个默认 API 版本，例如 `v1`。
- 一个版本注册表，用于把版本字符串映射到具体工厂。
- 一个 `CreateOpenAPI(config)` 风格入口，返回聚合客户端。

这样可以保持使用方式一致，也为未来支持多个 API 版本留下稳定扩展点。

备选方案是直接实例化 `V1OpenAPIClient`。该方案更直接，但会把版本选择暴露到业务方，后续升级时兼容性更差，因此不采用。

### 3. 基础请求器统一注入鉴权、User-Agent 与 sandbox URL
参考 Node SDK 的 `addUserAgent`、`addAuthorization`、`buildUrl` 行为，C++ 侧将把这三项下沉到请求器层统一处理：
- `Authorization: Bot <app_id>.<token>`。
- SDK 版本化 `User-Agent`。
- 根据 `sandbox` 选择正式或沙箱域名。

这样业务 API 模块只需要描述方法、路径和 payload，不需要重复处理公共头和 URL 拼装。

备选方案是由每个 API 模块自行拼接请求。该方案重复多、出错面大，不采用。

### 4. WebSocket 采用“Client + Session + ConnectionState”模型
参考 Node SDK 的 `WebsocketClient -> Session -> Ws` 三层结构，C++ 保留类似职责：
- `WebSocketClient`：对外入口、事件订阅接口、重试策略。
- `Session`：负责获取网关地址、创建连接、关闭会话。
- `GatewayConnection`：处理 HELLO、IDENTIFY/RESUME、HEARTBEAT、DISPATCH、CLOSE 等协议细节。
- `SessionState`：保存 `session_id`、`seq`、重连上下文。

该模型适合 C++ 显式管理生命周期，也能把协议状态与用户接口解耦。

备选方案是只保留一个 WebSocket 类处理全部逻辑。该方案测试性和可维护性较差，因此不采用。

### 5. 事件分发采用 typed event + 原始 payload 并存
为了兼顾易用性和协议兼容，事件回调接口同时暴露：
- 标准化事件类型枚举。
- 原始事件名字符串。
- 原始 JSON payload。
- 若已支持则提供解析后的强类型结构。

这样在初始阶段即使还没为所有事件建立强类型结构，业务方也可以先消费原始载荷；后续再逐步增强类型能力。

### 6. 错误模型统一为 SDKError，并保留 trace_id / HTTP 状态 / 网关关闭码
参考 Node SDK 对 HTTP 错误的简化包装，C++ 侧统一定义 `SDKError`：
- HTTP 请求失败时保留状态码、服务端错误体、trace ID。
- WebSocket 断开时保留 close code、是否允许 resume、最近会话记录。
- 对外暴露可读错误消息与程序可判定字段。

这样可以减少上层对具体网络库异常类型的依赖。

## Risks / Trade-offs

- [底层库选型未最终确定] → 先抽象 `HttpTransport`、`WebSocketTransport`、`JsonCodec` 接口，避免上层绑定具体实现。
- [Node SDK 与 C++ 异步模型差异大] → 将回调、会话状态和重试逻辑分层，避免在 facade 暴露复杂线程细节。
- [初期只实现骨架可能无法覆盖全部业务 API] → 先保证统一入口、基础请求器和网关流程稳定，业务 API 按优先级逐步补齐。
- [事件类型系统过早设计过重] → 初期允许原始 payload 透传，强类型结构按高频事件逐步引入。
- [重连与心跳实现容易出现状态竞争] → 将 `session_id`、`seq`、alive 状态集中到 `SessionState`，并定义单一更新路径。

## Migration Plan

1. 在 `src/` 下创建 C++ SDK 的基础目录结构与公共头文件。
2. 先实现配置、常量、错误模型和 transport 抽象。
3. 实现版本化 OpenAPI 骨架、默认 `v1` 客户端和少量核心 API 模块样板。
4. 实现网关地址获取、WebSocket 建连、心跳、重连与事件派发。
5. 增加最小可运行示例或测试，验证 HTTP 与 WebSocket 主链路。
6. 后续若实现不符合预期，可回滚到仅保留目录结构与接口层，不对外发布不稳定实现。

## Open Questions

- 最终采用哪套 C++ 网络库来实现 HTTP、WebSocket 与 JSON：Boost.Beast、libcurl + websocketpp、还是其他组合？
- 是否需要从第一版开始就同时提供同步与异步 OpenAPI 调用接口？
- 对外 API 更偏向异常抛出还是 `expected/result` 风格返回？
- 是否需要在第一版就兼容 Windows/MSVC 与 Linux/gcc/clang 的统一构建方案？
